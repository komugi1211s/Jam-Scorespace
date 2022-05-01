#define fz_NO_WINDOWS_H
#define FUZZY_MY_H_IMPL
#include "my.h"

#include <raylib.h>
#include <raymath.h>

#define WINDOW_WIDTH  1200
#define WINDOW_HEIGHT  900 

#define MAP_SIZE  700
#define TILE_SIZE (MAP_SIZE / 28.0f)
#define HALF_TILE (TILE_SIZE * 0.5f)

const float MAP_X_BEGIN  = ((WINDOW_WIDTH - MAP_SIZE) * 0.5);
const float MAP_X_END    = ((WINDOW_WIDTH + MAP_SIZE) * 0.5);
const float MAP_X_CENTER = (MAP_X_BEGIN + ((MAP_X_END - MAP_X_BEGIN) * 0.5));

const float MAP_Y_BEGIN  = ((WINDOW_HEIGHT - MAP_SIZE) * 0.5);
const float MAP_Y_END    = ((WINDOW_HEIGHT + MAP_SIZE) * 0.5);
const float MAP_Y_CENTER = (MAP_Y_BEGIN + ((MAP_Y_END - MAP_Y_BEGIN) * 0.5));

struct Player {
    int   health;
    float charge_amount;
    int   holding_charge;

    int     performing_walljump;
    float   jump_timer;
    Vector2 next_normal;

    Vector2 pos;
    Vector2 size;
    Vector2 normal;
    Vector2 shoot_direction;
};

struct Game {
    int     is_playing;
    int     score;

    int     additional_score;
    int     combo;
    float   combo_timer;

    float   timescale;
    float   camerashake;

    int     hitting_wall;
    Vector2 hit_pos;

    int captured_entity[256];
    int captured_entity_count;
};

enum {
    ENTITY_NONE,
    ENTITY_ENEMY,
    ENTITY_BULLET,
    ENTITY_DEATH,
};

struct Entity {
    int type;
    int generation;

    int being_destroyed;

    Vector2 position;

    float cooldown;

    Vector2 direction;
    Vector2 target;
};

struct Interval {
    float max;
    float current;

    Interval(float m): max(m), current(m) {}
};

struct Ground {
    Vector2 begin;
    Vector2 end;

    Vector2 normal;
};

static Entity entities[512];

static Camera2D camera = {0};
static Player player = {0};

static Ground grounds[4];

static Game game;
static Font main_font;
static Font bigger_font;
const int MAINFONTSIZE = 64;
const int BIGFONTSIZE  = 128;

enum {
    SOUND_GOT_HIT,
    SOUND_SHOT_BULLET,
    SOUND_ENEMY_DIED,
    SOUND_TELEPORTED,
};


static Sound sounds[8];

inline float timescaled_dt() {
    return game.timescale * 0.016;
}

void do_debug_draw() {
    Vector2 pos = { 10, 10 };
    DrawText(TextFormat("Normal: [%f,%f]\n", player.normal.x, player.normal.y), pos.x, pos.y, 10, BLACK);

    pos.y += 10;
    if(player.performing_walljump) {
        DrawText(TextFormat("Performing Wall jump to: [%f,%f]\n", player.next_normal.x, player.next_normal.y), pos.x, pos.y, 10, BLACK);
        pos.y += 10;
    }

    pos.y += 10;
    for(int i = 0; i < fz_COUNTOF(entities); ++i) {
        if (entities[i].type != ENTITY_NONE) {
            DrawText(TextFormat("Entity: %d", entities[i].type), pos.x, pos.y, 10, BLACK);
            pos.y += 10;
        }
    }
}

void reset_game_state(int begin_play) {
    game.is_playing = begin_play;

    if (game.is_playing) {
        player.health = 5;
        player.charge_amount = 0;
        player.pos    = { MAP_SIZE * 0.5, MAP_SIZE * 0.5 };
        player.normal = { 0, -1 };
        player.performing_walljump = 0;

        game.score = 0;
        game.combo = 0;
        game.combo_timer = 0;
        game.hitting_wall = -1;
        game.timescale = 1;
    }
}

int interval_tick(Interval *it, float dt) {
    it->current -= dt;
    if (it->current < 0) {
        it->current = it->max;
        return 1;
    }
    return 0;
}

int spawn_entity(int type) {
    int entity_id = 0;
    for(int i = 1; i < fz_COUNTOF(entities); ++i) {
        if (entities[i].type == ENTITY_NONE) {
            entity_id = i;
            break;
        }
    }

    entities[entity_id].type = type;
    entities[entity_id].generation = (entities[entity_id].generation + 1) % (INT_MAX - 1);
    entities[entity_id].being_destroyed = 0;

    return entity_id;
}

void do_enemy_update(Entity *e) {
    e->cooldown -= timescaled_dt();
    e->position = Vector2Lerp(e->position, e->target, 0.25);

    if (e->cooldown < 0) {
        int id = spawn_entity(ENTITY_BULLET);
        if (id) {
            Entity *bullet = &entities[id];

            bullet->position  = e->position;
            bullet->direction = Vector2Normalize(Vector2Subtract(player.pos, e->position));

            e->cooldown = 2.0f;

            Vector2 player_pos = Vector2Normalize(player.pos);
            Vector2 enemy_pos  = Vector2Normalize(e->position);

            int x_pos = GetRandomValue((int)MAP_X_BEGIN + 100, (int)MAP_X_END - 100);
            int y_pos = GetRandomValue((int)MAP_Y_BEGIN + 100, (int)MAP_Y_END - 100);

            e->target = { (float)x_pos, (float)y_pos };

            PlaySoundMulti(sounds[SOUND_SHOT_BULLET]);
        }
    }
}

void do_bullet_update(Entity *e) {
    e->position = Vector2Add(e->position, e->direction);

    Rectangle player_rec = { player.pos.x, player.pos.y, player.size.x, player.size.y };
    if (CheckCollisionCircleRec(e->position, 4, player_rec)) {
        player.health -= 1;
        e->being_destroyed = 1;

        game.camerashake += 0.15;
        PlaySoundMulti(sounds[SOUND_GOT_HIT]);
    }
}

void update_entities() {
    for(int i = 0; i < fz_COUNTOF(entities); ++i) {
        Entity *e = &entities[i];

        if (e->type == ENTITY_NONE) continue;
        if ((e->position.x < MAP_X_BEGIN) || (MAP_X_END < e->position.x)) {
            printf("Entity %d is dying due to outside X: %f \n", i, e->position.x);
            printf("map-x: %f %f \n", MAP_X_BEGIN, MAP_X_END);
            e->being_destroyed = 1;
        }
        if ((e->position.y < MAP_Y_BEGIN) || (MAP_Y_END < e->position.y)) {
            printf("Entity %d is dying due to outside Y: %f \n", i, e->position.y);
            printf("map-y: %f %f \n", MAP_Y_BEGIN, MAP_Y_END);
            e->being_destroyed = 1;
        }

        if (e->being_destroyed) {
            printf("Entity %d being destroyed\n", i);
            e->type = ENTITY_NONE;
            continue;
        }

        switch(e->type) {
            case ENTITY_ENEMY:  do_enemy_update(e);  break;
            case ENTITY_BULLET: do_bullet_update(e); break;

            default:
                assert(!"What the heck!?");
        }
    }
}

void get_magnetbeam_line(Vector2 *begin, Vector2 *ends) {
    *begin = Vector2Add(player.pos, { HALF_TILE, HALF_TILE });
    *ends  = Vector2Add(player.pos, Vector2Scale(player.shoot_direction, player.charge_amount * MAP_SIZE * 1.55));
}

void update_player_input(int x_axis, int charging, Vector2 mouse) {
    static float accel = 0;

    if (!player.performing_walljump) {
        // What a weird way to perform an acceleration.
        accel = Lerp(accel, (x_axis * 8), 0.25);
        Vector2 movedir = { -player.normal.y, -player.normal.x };

        Vector2 accele = Vector2Scale(movedir, accel);
        player.pos = Vector2Add(player.pos, accele);
        player.shoot_direction = Vector2Normalize(Vector2Subtract(mouse, Vector2Add(player.pos, { HALF_TILE, HALF_TILE })));

        player.holding_charge = charging;

        if (player.charge_amount > 1)      player.charge_amount = 1;
        else if (player.charge_amount < 0) player.charge_amount = 0;
    } else {
        player.charge_amount = 0;
    }
}

void do_player_update() {
    if (player.health <= 0) {
        game.is_playing = 0;
        return;
    }


    Vector2 norm = player.normal;


    if (!player.performing_walljump) {
        // what is this monstrosity?

        if (player.holding_charge) {
            player.charge_amount += timescaled_dt();
        }

        if (norm.y == -1) {
            player.pos.y = MAP_Y_END - TILE_SIZE;
        } else if (norm.y == 1) {
            player.pos.y = MAP_Y_BEGIN;
        } else if (norm.x == 1) {
            player.pos.x = MAP_X_BEGIN;
        } else if (norm.x == -1) {
            player.pos.x = MAP_X_END - TILE_SIZE;
        } else {
            assert(!"how did this happen?");
        }

        if (player.pos.x < MAP_X_BEGIN) player.pos.x = MAP_X_BEGIN;
        if (player.pos.x + TILE_SIZE > MAP_X_END)   player.pos.x = MAP_X_END - TILE_SIZE;
        if (player.pos.y < MAP_Y_BEGIN) player.pos.y = MAP_Y_BEGIN;
        if (player.pos.y + TILE_SIZE > MAP_Y_END)   player.pos.y = MAP_Y_END - TILE_SIZE;

        if ((!player.holding_charge) && (game.hitting_wall != -1)) { // Player released a hold -- wall jump imminent.
            Ground g = grounds[game.hitting_wall];

            player.performing_walljump = 1;
            player.next_normal = g.normal;
            player.jump_timer = 0.25;

            game.captured_entity_count = 0;

            Vector2 mlineb, mlinee;
            get_magnetbeam_line(&mlineb, &mlinee);

            int threshold = player.charge_amount * MAP_SIZE * 0.5;

            for(int i = 0; i < fz_COUNTOF(entities); ++i) {
                Entity *e = &entities[i];
                if (e->being_destroyed) continue;
                if (e->type == ENTITY_NONE) continue;

                if (e->type == ENTITY_ENEMY) {
                    if(CheckCollisionPointLine(e->position, mlineb, mlinee, threshold)) {
                        game.captured_entity[game.captured_entity_count++] = i;
                    }
                }
            }

            player.charge_amount = 0;
        }
    } else {
        player.jump_timer -= timescaled_dt();
        if (player.jump_timer < 0) {
            player.performing_walljump = 0;
            player.charge_amount = 0;
            player.holding_charge = 0;
            player.normal = player.next_normal;
            player.pos = game.hit_pos;
            game.hitting_wall = -1;
            game.camerashake = 0.25;

            PlaySoundMulti(sounds[SOUND_TELEPORTED]);
            if (game.captured_entity_count > 0) {
                PlaySoundMulti(sounds[SOUND_ENEMY_DIED]);
            }

            for (int i = 0; i < game.captured_entity_count; ++i) {
                int killing = game.captured_entity[i];
                entities[killing].being_destroyed = 1;
                game.additional_score += 10;
                game.combo       += 1;
                game.combo_timer = fmin(game.combo_timer + 1.0, 5.0);
                game.camerashake += 0.05;
            }
        } else if (player.jump_timer < 0.08) {
            player.pos = Vector2Lerp(player.pos, game.hit_pos, 0.25);
        } else {
            player.pos  = Vector2Add(player.pos, player.normal);
        }
    }
}

void game_update() {
    static Interval enemy_spawn_interval(1.5);

    if (game.camerashake > 0) {
        game.camerashake -= timescaled_dt();
        if (game.camerashake < 0) game.camerashake = 0;

        camera.offset.x = GetRandomValue(1, 50) * game.camerashake;
        camera.offset.y = GetRandomValue(1, 50) * game.camerashake;
    }

    if (game.is_playing) {
        if (game.combo_timer > 0) {
            game.combo_timer -= timescaled_dt();
            if (game.combo_timer < 0) {
                game.score += (game.additional_score * (1.0f + (game.combo * 0.01)));
                game.combo = 0;
                game.additional_score = 0;
            }
        }

        int axis_x = (-!!IsKeyDown(KEY_A)) + !!IsKeyDown(KEY_D);
        int charging = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
        Vector2 mouse = GetMousePosition();
        if(interval_tick(&enemy_spawn_interval, timescaled_dt())) {
            int id = spawn_entity(ENTITY_ENEMY);
            if (id) {
                Entity *e = &entities[id];
                e->cooldown = GetRandomValue(1, 100) * 0.01;

                float margin = MAP_SIZE * 0.15;
                e->position.x = GetRandomValue((int)(MAP_X_BEGIN + TILE_SIZE), (int)(MAP_X_END - TILE_SIZE));
                e->position.y = GetRandomValue((int)(MAP_Y_BEGIN + TILE_SIZE), (int)(MAP_Y_END - TILE_SIZE));
                e->target = e->position;
            }
        }

        update_player_input(axis_x, charging, mouse);
        float x = player.pos.x;
        if (player.holding_charge) {
            Vector2 mline_b, mline_e;
            get_magnetbeam_line(&mline_b, &mline_e);
            for (int i = 0; i < fz_COUNTOF(grounds); ++i) {
                Vector2 gline_b = grounds[i].begin;
                Vector2 gline_e = grounds[i].end;

                Vector2 col;
                if (CheckCollisionLines(mline_b, mline_e, gline_b, gline_e, &col)) {
                    game.hitting_wall = i;
                    game.hit_pos      = col;
                    break;
                }
            }
        }

        do_player_update();
        update_entities();
    }
}

void draw_enemy(Entity *e) {
    DrawCircleLines(e->position.x, e->position.y, 8, BLACK);
}

void draw_bullet(Entity *e) {
    DrawCircleLines(e->position.x, e->position.y, 4, BLACK);
}

void draw_combo_indicator() {
    Color c = Fade(BLACK, 0.2);

    const char *text = TextFormat("%06d", game.score);
    Vector2 size = MeasureTextEx(bigger_font, text, BIGFONTSIZE, 0);

    float x = MAP_X_CENTER - (size.x * 0.5);
    float y = MAP_Y_CENTER - (size.y * 0.5);

    DrawTextEx(bigger_font, text, {x,y}, BIGFONTSIZE, 0, c);
    y = MAP_Y_CENTER + (size.y * 0.5);

    if (game.additional_score > 0) {
        const char *text = TextFormat("+%d", game.additional_score);
        Vector2 size = MeasureTextEx(main_font, text, MAINFONTSIZE, 0);
        float x = MAP_X_CENTER - (size.x * 0.5);
        DrawTextEx(main_font, text, {x,y}, MAINFONTSIZE, 0, c);

        y += size.y;
    }

    if (game.combo_timer > 0) {
        const char *text = TextFormat("%d combo (%01.2f s)", game.combo, game.combo_timer);
        Vector2 size = MeasureTextEx(main_font, text, MAINFONTSIZE, 0);
        float x = MAP_X_CENTER - (size.x * 0.5);

        DrawTextEx(main_font, text, {x,y}, MAINFONTSIZE, 0, c);
    }
}

void draw_game_screen(RenderTexture2D game_tex) {
    // ===================================
    // Inside Render Buffer.
    BeginTextureMode(game_tex);
    ClearBackground(WHITE);
    DrawRectangleV(player.pos, player.size, BLACK);

    if(player.charge_amount > 0) {
        Vector2 begin, end;
        get_magnetbeam_line(&begin, &end);
        if (game.hitting_wall != -1) {
            end = game.hit_pos;
        }
        DrawLineV(begin, end, BLACK);
    }

    for (int i = 0; i < 4; ++i) {
        Color color = BLACK;
        if (game.hitting_wall == i) {
            color = RED;
        }
        Ground ground = grounds[i];
        DrawLineEx(ground.begin, ground.end, 2, color);
    }

    for(int i = 0; i < fz_COUNTOF(entities); ++i) {
        Entity *e = &entities[i];
        if (e->being_destroyed) continue;
        if (e->type == ENTITY_NONE) continue;

        switch(e->type) {
            case ENTITY_ENEMY:  draw_enemy(e);  break;
            case ENTITY_BULLET: draw_bullet(e); break;
        }
    }
    //
    // ===================================
    // Outside Render Buffer.
    draw_combo_indicator();
    EndTextureMode();
}

int main(int argc, char **argv) {
    InitWindow(1200, 900, "Gravitas");
    InitAudioDevice();
    SetTargetFPS(60);

    game.timescale = 1;

    camera.zoom = 1;
    player.normal.x = 1;
    player.size = { TILE_SIZE, TILE_SIZE };

    player.pos.x = MAP_SIZE * 0.5;
    player.pos.y = MAP_SIZE * 0.5;
    float offx = (1200 - MAP_SIZE) * 0.5;
    float offy = ( 900 - MAP_SIZE) * 0.5;

    grounds[0].begin  = { MAP_X_BEGIN,  MAP_Y_BEGIN };
    grounds[0].end    = { MAP_X_END,    MAP_Y_BEGIN };
    grounds[0].normal = { 0,  1 };

    grounds[1].begin  = { MAP_X_BEGIN, MAP_Y_BEGIN };
    grounds[1].end    = { MAP_X_BEGIN, MAP_Y_END   };
    grounds[1].normal = { 1, 0 };

    grounds[2].begin = { MAP_X_END,  MAP_Y_BEGIN };
    grounds[2].end   = { MAP_X_END,  MAP_Y_END   };
    grounds[2].normal = { -1, 0 };

    grounds[3].begin = { MAP_X_BEGIN,  MAP_Y_END  };
    grounds[3].end   = { MAP_X_END,    MAP_Y_END  };
    grounds[3].normal = { 0, -1 };

    sounds[SOUND_GOT_HIT] = LoadSound("assets/sounds/got_hit.wav");
    sounds[SOUND_SHOT_BULLET] = LoadSound("assets/sounds/bullet_shot.wav");
    sounds[SOUND_ENEMY_DIED] = LoadSound("assets/sounds/enemy_died.wav");
    sounds[SOUND_TELEPORTED] = LoadSound("assets/sounds/teleport.wav");

    float accum = 0;
    RenderTexture2D game_tex = LoadRenderTexture(1200, 900);
    SetTextureFilter(game_tex.texture, TEXTURE_FILTER_BILINEAR);

    main_font = LoadFontEx("assets/fonts/Poppins-SemiBold.ttf", MAINFONTSIZE, 0, 0);
    bigger_font = LoadFontEx("assets/fonts/Poppins-SemiBold.ttf", BIGFONTSIZE, 0, 0);

    reset_game_state(1);

    while(!WindowShouldClose()) {
        accum += GetFrameTime();
        while(accum > 0.016) {
            accum -= 0.016;
            game_update();

            if (accum < 0) accum = 0;
        }

        draw_game_screen(game_tex);

        BeginDrawing();
        BeginMode2D(camera);
        ClearBackground(WHITE);

        Rectangle swapped = { 0.0f, 0.0f, (float)game_tex.texture.width, (float)-game_tex.texture.height};
        Rectangle to = {MAP_X_CENTER, MAP_Y_CENTER, 1200, 900};


        DrawTexturePro(game_tex.texture, swapped, to, {MAP_X_CENTER, MAP_Y_CENTER}, 0, WHITE);

        do_debug_draw();
        EndMode2D();
        EndDrawing();
    }

    UnloadFont(main_font);
    UnloadFont(bigger_font);
    UnloadRenderTexture(game_tex);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
