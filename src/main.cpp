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

enum {
    STATE_TITLE_SCREEN,
    STATE_TUTORIAL,
    STATE_PLAYING,
    STATE_PLAYER_DIED,
    STATE_LEADERBOARD,
};

struct Game {
    int     state;
    int     score;

    int     tutorial_happened;

    float   state_change_timer;
    float   state_change_max;

    int     additional_score;
    int     combo;
    float   combo_timer;

    float   timescale;
    float   camerashake;

    int     hitting_wall;
    Vector2 hit_pos;

    int captured_entity[256];
    int captured_entity_count;

    int high_score[32];
    int high_score_count;
};

enum {
    ENTITY_NONE,
    ENTITY_ENEMY,
    ENTITY_BULLET,
    ENTITY_DEATH,
};

struct Entity {
    int type;
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

static Entity entities[1024];

static Camera2D camera = {{0}};
static Player player = {0};

static Ground grounds[4];

static Game game;
static Font main_font;
static Font bigger_font;
const int MAINFONTSIZE = 58;
const int BIGFONTSIZE  = 96;

enum {
    SOUND_GOT_HIT,
    SOUND_SHOT_BULLET,
    SOUND_ENEMY_DIED,
    SOUND_TELEPORTED,
    SOUND_SPAWN_ENEMY,
};

static Sound sounds[8];
static Music game_music;

inline float timescaled_dt() {
    return game.timescale * 0.016;
}

inline float combo_multiplier() {
    return (1.0f + (game.combo * 0.01));
}

inline int calc_additional_score() {
    return (game.additional_score * combo_multiplier());
}

inline int get_magnetbeam_threshold() {
    return player.charge_amount * MAP_SIZE * 0.25;
}

void get_magnetbeam_line(Vector2 *begin, Vector2 *ends) {
    *begin = Vector2Add(player.pos, { HALF_TILE, HALF_TILE });
    *ends  = Vector2Add(*begin, Vector2Scale(player.shoot_direction, player.charge_amount * MAP_SIZE * 1.55));
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

void update_music() {
    static float music_fade = 0;
    if (!IsMusicStreamPlaying(game_music)) {
        PlayMusicStream(game_music);
        SeekMusicStream(game_music, 0);
    }

    if ((GetMusicTimeLength(game_music) - GetMusicTimePlayed(game_music)) < 0.25) {
        SeekMusicStream(game_music, 0);
    }

    float last_frame = music_fade;
    music_fade = Lerp(music_fade, !!(game.state == STATE_PLAYING), 0.05);

    if(last_frame == 0.0 && game.state == STATE_PLAYING) {
        ResumeMusicStream(game_music);
    }
    else if(music_fade == 0.0 && game.state != STATE_PLAYING) {
        PauseMusicStream(game_music);
    }

    SetMusicPitch(game_music, music_fade);
    UpdateMusicStream(game_music);
}

void change_game_state(int state, float time) {
    game.state = state;
    game.state_change_timer = time;
    game.state_change_max   = time;
    game.hitting_wall = -1;

    if (game.state == STATE_PLAYING) {
        for (int i = 0; i < fz_COUNTOF(entities); ++i) {
            entities[i].type = 0;
        }

        player.charge_amount = 0;
        player.pos    = { MAP_X_CENTER - HALF_TILE, MAP_Y_CENTER - HALF_TILE};
        player.normal = { 0, -1 };
        player.performing_walljump = 0;

        game.score = 0;
        game.combo = 0;
        game.additional_score = 0;
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

int scoresort(const void *a, const void *b) {
    return (*(const int*)b) - (*(const int *)a);
}

void perform_player_death() {
    game.score += calc_additional_score();
    game.additional_score = 0;
    game.combo = 0;
    game.combo_timer = 0;

    int found = -1;
    int minimum = game.score;
    for(int i = 0; i < sizeof(game.high_score) / sizeof(int); ++i) {
        if (game.high_score[i] < minimum) {
            found = i;
            minimum = game.high_score[i];
        }
    }
    if (found != -1) {
        game.high_score[found] = game.score;
    }

    qsort(game.high_score, sizeof(game.high_score) / sizeof(int), sizeof(int), scoresort);
    change_game_state(STATE_PLAYER_DIED, 1.0);
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

            int x_pos = GetRandomValue((int)MAP_X_BEGIN + 100, (int)MAP_X_END - 100);
            int y_pos = GetRandomValue((int)MAP_Y_BEGIN + 100, (int)MAP_Y_END - 100);

            e->target = { (float)x_pos, (float)y_pos };

            PlaySoundMulti(sounds[SOUND_SHOT_BULLET]);
        }
    }
}

void do_bullet_update(Entity *e) {
    e->position = Vector2Add(e->position, Vector2Scale(e->direction, timescaled_dt() * 60));
    Rectangle player_rec = { player.pos.x, player.pos.y, player.size.x, player.size.y };
    if (CheckCollisionCircleRec(e->position, 4, player_rec)) {
        if (!player.performing_walljump) {
            e->being_destroyed = 1;
            game.camerashake += 0.15;
            PlaySoundMulti(sounds[SOUND_GOT_HIT]);

            perform_player_death();
        }
        e->being_destroyed = 1;
    }
}

void do_death_update(Entity *e) {
    e->cooldown -= timescaled_dt();
    if (e->cooldown < 0) {
        e->being_destroyed = 1;
    }
}

void update_entities() {
    for(int i = 0; i < fz_COUNTOF(entities); ++i) {
        Entity *e = &entities[i];

        if (e->type == ENTITY_NONE) continue;
        if ((e->position.x < MAP_X_BEGIN) || (MAP_X_END < e->position.x)) {
            e->being_destroyed = 1;
        }
        if ((e->position.y < MAP_Y_BEGIN) || (MAP_Y_END < e->position.y)) {
            e->being_destroyed = 1;
        }

        if (e->being_destroyed) {
            e->type = ENTITY_NONE;
            continue;
        }

        switch(e->type) {
            case ENTITY_ENEMY:  do_enemy_update(e);  break;
            case ENTITY_BULLET: do_bullet_update(e); break;
            case ENTITY_DEATH:  do_death_update(e);  break;

            default:
                assert(!"What the heck!?");
        }
    }
}

void update_player_input(int x_axis, int charging, Vector2 mouse) {
    static float accel = 0;

    if (!player.performing_walljump) {
        // What a weird way to perform an acceleration.
        accel = Lerp(accel, (x_axis * 8), 8 * timescaled_dt());
        Vector2 movedir = { -player.normal.y, -player.normal.x };

        Vector2 accele = Vector2Scale(movedir, accel);
        player.pos = Vector2Add(player.pos, accele);
        player.shoot_direction = Vector2Normalize(Vector2Subtract(mouse, Vector2Add(player.pos, { HALF_TILE, HALF_TILE })));

        player.holding_charge = charging;

        if (player.charge_amount > 1)      player.charge_amount = 1;
        else if (player.charge_amount < 0) player.charge_amount = 0;
    }
}

void do_player_update() {
    Vector2 norm = player.normal;
    if (!player.performing_walljump) {
        // what is this monstrosity?

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

        if (player.holding_charge) {
            player.charge_amount += timescaled_dt();
        } else {
            if (game.hitting_wall != -1) {
                Ground g = grounds[game.hitting_wall];

                player.performing_walljump = 1;
                player.next_normal = g.normal;
                player.jump_timer = 0.25;

                game.captured_entity_count = 0;

                Vector2 mlineb, mlinee;
                get_magnetbeam_line(&mlineb, &mlinee);
                int threshold = get_magnetbeam_threshold() * 0.5;

                for(int i = 0; i < fz_COUNTOF(entities); ++i) {
                    Entity *e = &entities[i];
                    if (e->being_destroyed) continue;
                    if (e->type == ENTITY_NONE) continue;

                    if (e->type == ENTITY_ENEMY) {
                        if(CheckCollisionPointLine(e->position, mlineb, mlinee, threshold)) {
                            game.captured_entity[game.captured_entity_count++] = i;

                            Vector2 linenorm = Vector2Normalize(Vector2Subtract(mlinee, mlineb));
                            Vector2 begin = Vector2Subtract(e->position, mlineb);
                            float dist = Vector2DotProduct(begin, linenorm);

                            e->cooldown = 100000.0;
                            e->target = Vector2Add(mlineb, Vector2Scale(linenorm, dist));
                        }
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
                game.timescale = 0.01;
                PlaySoundMulti(sounds[SOUND_ENEMY_DIED]);
            }

            for (int i = 0; i < game.captured_entity_count; ++i) {
                int killing = game.captured_entity[i];
                entities[killing].type = ENTITY_DEATH;
                entities[killing].cooldown = 1.0;

                game.additional_score += 50;
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

static Interval enemy_spawn_interval = Interval(1.0);

void game_update() {
    update_music();
    if (game.camerashake > 0) {
        game.camerashake -= timescaled_dt();
        if (game.camerashake < 0) game.camerashake = 0;

        camera.offset.x = GetRandomValue(1, 50) * game.camerashake;
        camera.offset.y = GetRandomValue(1, 50) * game.camerashake;
    }

    if(game.timescale < 1.0) game.timescale += 0.5;
    else if(game.timescale > 1.0) game.timescale = 0;

    game.state_change_timer -= timescaled_dt();
    if (game.state_change_timer < 0.0) game.state_change_timer = 0.0;

    int axis_x = (-!!IsKeyDown(KEY_A)) + !!IsKeyDown(KEY_D);
    int charging = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
    Vector2 mouse = GetMousePosition();

    switch(game.state) {
        case STATE_LEADERBOARD:
        {
            if (game.state_change_timer <= 0.0) {
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    change_game_state(STATE_PLAYING, 0.5);
                }
            }
            update_player_input(axis_x, 0, mouse);
            do_player_update();
        } break;

        case STATE_TUTORIAL:
        {
            if (game.state_change_timer <= 0.0) {
                game.tutorial_happened = 1;
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    change_game_state(STATE_PLAYING, 0.5);
                }
            }
            update_player_input(axis_x, 0, mouse);
            do_player_update();
        } break;

        case STATE_PLAYER_DIED:
        {
            if (game.state_change_timer <= 0.0) {
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    change_game_state(STATE_PLAYING, 0.5);
                }
                if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
                    change_game_state(STATE_LEADERBOARD, 0.5);
                }
            }
            update_player_input(axis_x, 0, mouse);
            do_player_update();
        } break;
        case STATE_TITLE_SCREEN:
        {
            if (game.state_change_timer <= 0.0) {
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    change_game_state(game.tutorial_happened ? STATE_PLAYING : STATE_TUTORIAL, 0.5);
                }
            }
            update_player_input(axis_x, 0, mouse);
            do_player_update();
        } break;

        case STATE_PLAYING:
        {
            if (game.state_change_timer <= 0.0) {
                if (game.combo_timer > 0) {
                    game.combo_timer -= timescaled_dt();
                    if (game.combo_timer < 0) {
                        game.score += calc_additional_score();

                        game.combo = 0;
                        game.additional_score = 0;
                    }
                }

                if(interval_tick(&enemy_spawn_interval, timescaled_dt())) {
                    int id = spawn_entity(ENTITY_ENEMY);
                    if (id) {
                        Entity *e = &entities[id];
                        e->cooldown = GetRandomValue(1, 100) * 0.01;
                        e->position.x = GetRandomValue((int)(MAP_X_BEGIN + TILE_SIZE), (int)(MAP_X_END - TILE_SIZE));
                        e->position.y = GetRandomValue((int)(MAP_Y_BEGIN + TILE_SIZE), (int)(MAP_Y_END - TILE_SIZE));
                        e->target = e->position;

                        PlaySoundMulti(sounds[SOUND_SPAWN_ENEMY]);
                    }
                }

                update_player_input(axis_x, charging, mouse);
                if (player.holding_charge) {
                    Vector2 mline_b, mline_e;
                    get_magnetbeam_line(&mline_b, &mline_e);
                    for (int i = 0; i < fz_COUNTOF(grounds); ++i) {
                        Vector2 gline_b = grounds[i].begin;
                        Vector2 gline_e = grounds[i].end;

                        if(grounds[i].normal.x == player.normal.x &&
                           grounds[i].normal.y == player.normal.y)
                        {
                            continue;
                        }

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
        } break;
    }
}

void draw_enemy(Entity *e) {
    DrawCircle(e->position.x, e->position.y, 8, RED);
}

void draw_bullet(Entity *e) {
    DrawCircleLines(e->position.x, e->position.y, 4, BLACK);
}

void draw_death(Entity *e) {
    float posx = e->position.x;
    float posy = e->position.y - ((1.0f - e->cooldown) * 10);
    DrawText("50", posx, posy, 18, BLACK);
}

void draw_combo_indicator() {
    float state_delta = (game.state_change_max - game.state_change_timer) / game.state_change_max;
    Color c = Fade(BLACK, state_delta * (0.1 + (game.combo_timer / 10.0)));

    const char *text = TextFormat("%06d", game.score);
    Vector2 size = MeasureTextEx(bigger_font, text, BIGFONTSIZE, 0);

    float x = MAP_X_CENTER - (size.x * 0.5);
    float y = MAP_Y_CENTER - (size.y * 0.5);

    DrawTextEx(bigger_font, text, {x,y}, BIGFONTSIZE, 0, c);
    y = MAP_Y_CENTER + (size.y * 0.5);

    if (game.additional_score > 0) {
        const char *text = TextFormat("+%d", calc_additional_score());
        Vector2 size = MeasureTextEx(main_font, text, MAINFONTSIZE, 0);
        float x = MAP_X_CENTER - (size.x * 0.5);
        DrawTextEx(main_font, text, {x,y}, MAINFONTSIZE, 0, c);

        y += size.y;
    }

    if (game.combo_timer > 0) {
        const char *text = TextFormat("%d combo: %01.2f bonus (%01.2f s)", game.combo, combo_multiplier(), game.combo_timer);
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

    for (int i = 0; i < 4; ++i) {
        Color color = BLACK;
        if (game.hitting_wall == i) {
            color = RED;
        }
        Ground ground = grounds[i];
        DrawLineEx(ground.begin, ground.end, 4, color);
    }
    float state_delta = (game.state_change_max - game.state_change_timer) / game.state_change_max;

    switch(game.state) {
        case STATE_TITLE_SCREEN:
        {
            Color c = Fade(BLACK, (state_delta * state_delta));
            const char *text    = "Maglatch";
            const char *subtext = "Click left mouse to begin.";

            Vector2 size = MeasureTextEx(bigger_font, text, BIGFONTSIZE, 0);
            float x = MAP_X_CENTER - (size.x * 0.5);
            float y = (MAP_Y_CENTER * 0.75) - (size.y * 0.5);

            DrawTextEx(bigger_font, text,    {x,y}, BIGFONTSIZE, 0, c);

            y += size.y;
        } break;

        case STATE_TUTORIAL:
        {
            const char *message = {
                "LMB to charge the magnet beam",
                "",
                "Release LMB when beam is hitting the wall to",
                "perform wall jump.",
                "",
                "any enemy inside the beam's area will be a score!",
                "",
                "Don't get hit by enemy bullet!",
            };
        } break;

        case STATE_PLAYER_DIED:
        {
            Color c = Fade(BLACK, (1.0 - game.state_change_timer));
            const char *text    = "You died :(";
            const char *score   = TextFormat("Total Score: %d", game.score);
            const char *lmbmessage = "LMB - restart";
            const char *rmbmessage = "RMB - leaderboard";

            Vector2 size = MeasureTextEx(bigger_font, text, BIGFONTSIZE, 0);
            float x = MAP_X_CENTER - (size.x * 0.5);
            float y = (MAP_Y_CENTER * 0.85) - (size.y * 0.5);

            DrawTextEx(bigger_font, text, {x,y}, BIGFONTSIZE, 0, c);

            y += size.y;
            {
                Vector2 size = MeasureTextEx(main_font, score, MAINFONTSIZE, 0);
                x = MAP_X_CENTER - (size.x * 0.5);
                y = MAP_Y_CENTER + size.y * 0.5;
                DrawTextEx(main_font, score, {x,y}, MAINFONTSIZE, 0, c);
                y += size.y;
            }

            {
                Vector2 size = MeasureTextEx(main_font, lmbmessage, MAINFONTSIZE, 0);
                x = MAP_X_CENTER - (size.x * 0.5);
                DrawTextEx(main_font, lmbmessage, {x,y}, MAINFONTSIZE, 0, c);
                y += size.y;
            }

            {
                Vector2 size = MeasureTextEx(main_font, rmbmessage, MAINFONTSIZE, 0);
                x = MAP_X_CENTER - (size.x * 0.5);
                DrawTextEx(main_font, rmbmessage, {x,y}, MAINFONTSIZE, 0, c);
                y += size.y;
            }
        } break;

        case STATE_LEADERBOARD:
        {
            Color c = Fade(BLACK, (1.0 - game.state_change_timer));
            const char *text    = "Top 5 high score";

            Vector2 size = MeasureTextEx(bigger_font, text, BIGFONTSIZE, 0);
            float x = MAP_X_CENTER - (size.x * 0.5);
            float y = (MAP_Y_CENTER * 0.85) - (size.y * 0.5);
            DrawTextEx(bigger_font, text, {x,y}, BIGFONTSIZE, 0, c);

            y = (MAP_Y_CENTER) + (size.y * 0.5);

            for(int i = 0; i < 5; ++i) {
                int score = game.high_score[i];
                if (score != 0) {
                    const char *msg = TextFormat("%d: %06d", i + 1, score);
                    {
                        Vector2 size = MeasureTextEx(main_font, msg, MAINFONTSIZE, 0);
                        x = MAP_X_CENTER - (size.x * 0.5);
                        DrawTextEx(main_font, msg, {x,y}, MAINFONTSIZE, 0, c);
                        y += size.y;
                    }
                }
            }
        } break;

        case STATE_PLAYING:
        {
            BeginScissorMode(MAP_X_BEGIN, MAP_Y_BEGIN, MAP_SIZE, MAP_SIZE);
            if(player.charge_amount > 0) {
                Vector2 begin, end;
                get_magnetbeam_line(&begin, &end);
                if (game.hitting_wall != -1) {
                    end = game.hit_pos;
                }
                DrawLineEx(begin, end, get_magnetbeam_threshold(), Fade(BLUE, 0.05));
                DrawLineEx(begin, end, 2, BLUE);
            }

            for(int i = 0; i < fz_COUNTOF(entities); ++i) {
                Entity *e = &entities[i];
                if (e->being_destroyed) continue;
                if (e->type == ENTITY_NONE) continue;

                switch(e->type) {
                    case ENTITY_ENEMY:  draw_enemy(e);  break;
                    case ENTITY_BULLET: draw_bullet(e); break;
                    case ENTITY_DEATH: draw_death(e); break;
                }
            }
            //
            // ===================================
            // Outside Render Buffer.
            draw_combo_indicator();
            EndScissorMode();
        } break;
    }
    EndTextureMode();
}

int main(int argc, char **argv) {
    InitWindow(1200, 900, "Gravitas");
    InitAudioDevice();
    SetTargetFPS(60);

    game.timescale = 1;

    camera.zoom = 1;
    camera.rotation = 0;
    player.normal = { 0, -1 };
    player.size = { TILE_SIZE, TILE_SIZE };

    player.pos.x = MAP_SIZE * 0.5;
    player.pos.y = MAP_SIZE * 0.5;

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
    sounds[SOUND_SPAWN_ENEMY] = LoadSound("assets/sounds/enemy_spawn.wav");
    game_music = LoadMusicStream("assets/sounds/bgm.wav");

    float accum = 0;
    RenderTexture2D game_tex = LoadRenderTexture(1200, 900);
    SetTextureFilter(game_tex.texture, TEXTURE_FILTER_BILINEAR);

    main_font = LoadFontEx("assets/fonts/Poppins-Regular.ttf", MAINFONTSIZE, 0, 0);
    bigger_font = LoadFontEx("assets/fonts/Poppins-SemiBold.ttf", BIGFONTSIZE, 0, 0);

    change_game_state(STATE_TITLE_SCREEN, 1.0);

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

        EndMode2D();
        EndDrawing();
    }

    UnloadFont(main_font);
    UnloadFont(bigger_font);
    UnloadRenderTexture(game_tex);

    UnloadSound(sounds[SOUND_GOT_HIT]);
    UnloadSound(sounds[SOUND_SHOT_BULLET]);
    UnloadSound(sounds[SOUND_ENEMY_DIED]);
    UnloadSound(sounds[SOUND_TELEPORTED]);
    UnloadSound(sounds[SOUND_SPAWN_ENEMY]);
    UnloadMusicStream(game_music);

    CloseAudioDevice();
    CloseWindow();
    return 0;
}
