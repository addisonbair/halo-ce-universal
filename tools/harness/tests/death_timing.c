/*
DEATH_TIMING.C (test)

main.c's death and respawn timers, co-op respawn (players.c) and the dead
camera (dead_camera.c) over a fake world of up to four players, at frame
rates from 15 to 1000 a second: the delays are game ticks whatever the frame
rate, pause and cinematics hold them, a clock moved back by a load resets
them, a blocked co-op respawn retries. test_death_timing.py takes the code.
*/

#include "harness.h"

#define EXPECT(condition) CHECK(condition, "in %s", __func__)
#include <float.h>
typedef struct {real yaw,pitch;} real_euler_angles2d;
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define match_vassert(...) ((void)0)
static struct {short loss_timer,respawn_timer;boolean lost_map,respawn,saving_map,reset_map,revert_map;long switch_to_structure_bsp_index;} main_globals;
static long main_loss_last_tick,main_respawn_last_tick;
static long tick;
static int paused,cinematic,reverts,attempts,spawns,teleports,player_count=2;
static int projectiles,enemies,airborne,vehicle_moving,spawn_failed,teleport_failed,multiplayer;
static struct {int respawn_failure,respawn_failed;} players_state,*players_globals=&players_state;
struct player_datum {long unit_index,team_index;};
static struct player_datum players[4];
struct object_datum {struct {real_point3d bounding_sphere_center;} object;};
static struct object_datum objects[4];
struct biped_datum {struct {unsigned flags;} biped;};
static struct biped_datum biped;
struct players_vehicle_datum {struct {int unknown_state;} vehicle;};
static struct players_vehicle_datum vehicle;
struct data_iterator {long datum_index;int next;};
static int player_data;
/* (the network co-op paths, off: this is a local game) */
enum {_game_connection_local,_game_connection_network_client,_game_connection_network_server};
static short game_connection(void) {return _game_connection_local;}
static boolean network_coop_active(void) {return FALSE;}
static boolean players_respawn_network_coop(void) {return FALSE;}
static void players_respawn_at_checkpoint(void) {}
static boolean players_are_all_dead(void) {return FALSE;}
static boolean main_coop_host_revert(void) {return FALSE;}
enum {_biped_airborne_bit=0,_object_mask_vehicle=1};
static void data_iterator_new(struct data_iterator *i,int d) {(void)d;i->next=0;}
static void *data_iterator_next(struct data_iterator *i) {
    if(i->next>=player_count) return NULL;
    i->datum_index=i->next++;return &players[i->datum_index];
}
static struct player_datum *player_get(long i) {EXPECT(i>=0&&i<player_count);return &players[i];}
static struct object_datum *object_try_and_get(long i) {
    if(i==NONE) return NULL;
    EXPECT(i>=100&&i<104);return &objects[i-100];
}
static struct object_datum *object_get(long i) {return object_try_and_get(i);}
static long object_get_ultimate_parent(long i) {return vehicle_moving ? 999 : i;}
static struct biped_datum *biped_try_and_get(long i) {(void)i;biped.biped.flags=airborne;return &biped;}
static void *object_try_and_get_and_verify_type(long i,int mask) {
    (void)i;(void)mask;vehicle.vehicle.unknown_state=vehicle_moving;return &vehicle;
}
static int dangerous_projectiles_near_player(void) {return projectiles;}
static int any_unit_is_dangerous(void) {return 0;}
static int ai_enemies_attacking_player(void) {return enemies;}
static void player_spawn(long i) {spawns++;if(!spawn_failed) players[i].unit_index=100+i;}
static int player_teleport(long i,long target,const real_point3d *p) {
    (void)i;EXPECT(target==100);EXPECT(p->x==100);teleports++;return !teleport_failed;
}
static long game_time_get(void) {return tick;}
static int game_time_get_paused(void) {return paused;}
static int cinematic_in_progress(void) {return cinematic;}
static int game_engine_running(void) {return multiplayer;}
static void game_state_revert(void) {reverts++;}
static real_vector3d zero_vector,*global_zero_vector3d=&zero_vector;
static void vector3d_from_euler_angles2d(real_vector3d *v,const real_euler_angles2d *a) {
    (void)a;v->i=1;v->j=v->k=0;
}
static void observer_up_from_forward(const real_vector3d *f,real_vector3d *u) {
    (void)f;u->k=1;u->i=u->j=0;
}
static struct {real dead_timer,multiplayer_switch_timer,singleplayer_switch_timer;} dead_camera_constants={3,15,3};

#include "under_test.inc"

static void reset(int count) {
    int i;
    memset(&main_globals,0,sizeof(main_globals));memset(&players_state,0,sizeof(players_state));
    tick=main_loss_last_tick=main_respawn_last_tick=0;
    paused=cinematic=reverts=attempts=spawns=teleports=0;
    projectiles=enemies=airborne=vehicle_moving=spawn_failed=teleport_failed=multiplayer=0;
    player_count=count;
    for(i=0;i<count;i++) {players[i].unit_index=i?NONE:100;players[i].team_index=0;objects[i].object.bounding_sphere_center.x=100+i;}
}
static void rates(void) {
    int rates[]={15,30,60,120,144,240,1000};
    int r,frame,count;
    for(r=0;r<7;r++) {
        int hz=rates[r];
        reset(2);main_lost_map();
        for(frame=1;!reverts;frame++) {
            EXPECT(frame<hz*4);tick=(long)frame*30/hz;
            main_lost_map();main_lost_map_private();
            if(tick<92) EXPECT(!reverts);
        }
        EXPECT(reverts==1&&!main_globals.lost_map);
        printf("%d FPS: reload at %.3fs\n",hz,(frame-1)/(double)hz);
        for(count=2;count<=4;count++) {
            reset(count);main_respawn(FALSE);
            for(frame=1;main_globals.respawn;frame++) {
                EXPECT(frame<hz*4);tick=(long)frame*30/hz;
                main_respawn(FALSE);main_respawn_private();
                if(tick<92) EXPECT(!spawns&&!attempts);
            }
            EXPECT(spawns==count-1&&teleports==count-1&&!main_globals.lost_map);
        }
    }
}
static void clock_and_lifecycle(void) {
    int i;
    reset(2);main_lost_map();main_respawn(FALSE);
    for(i=0;i<10000;i++) {main_lost_map_private();main_respawn_private();}
    EXPECT(!reverts&&!attempts&&!main_globals.loss_timer&&!main_globals.respawn_timer);
    tick=40;main_lost_map_private();main_respawn_private();
    paused=1;tick=400;main_lost_map_private();main_respawn_private();
    EXPECT(main_globals.loss_timer==40&&main_globals.respawn_timer==40);
    paused=0;main_lost_map_private();main_respawn_private();EXPECT(!reverts&&!attempts);
    tick=451;main_lost_map_private();main_respawn_private();EXPECT(!reverts&&!attempts);
    tick=452;main_lost_map_private();main_respawn_private();EXPECT(reverts==1&&spawns==1);
    main_lost_map();players[1].unit_index=NONE;main_respawn(FALSE);
    EXPECT(!main_globals.loss_timer&&!main_globals.respawn_timer);
    tick=0;main_lost_map_private();main_respawn_private();
    tick=91;main_lost_map_private();main_respawn_private();EXPECT(reverts==1&&spawns==1);
    tick=92;main_lost_map_private();main_respawn_private();EXPECT(reverts==2&&spawns==2);
    reset(2);main_respawn(FALSE);tick=40;main_respawn_private();
    cinematic=1;tick=400;main_respawn_private();EXPECT(main_globals.respawn_timer==40);
    cinematic=0;main_respawn_private();EXPECT(!attempts);
    tick=452;main_respawn_private();EXPECT(spawns==1);
    reset(2);main_lost_map();cinematic=1;tick=92;main_lost_map_private();EXPECT(reverts==1);
    reset(2);main_respawn(TRUE);main_respawn_private();EXPECT(spawns==1);
    reset(2);main_respawn(TRUE);paused=1;main_respawn_private();EXPECT(!attempts);
    paused=0;main_respawn_private();EXPECT(spawns==1);
    reset(2);main_lost_map();tick=60;main_lost_map_private();
    main_reset_map();EXPECT(!main_globals.lost_map);tick=600;main_lost_map();
    main_lost_map_private();EXPECT(!reverts&&!main_globals.loss_timer);
    tick=692;main_lost_map_private();EXPECT(reverts==1);
    main_lost_map();tick=720;main_lost_map_private();main_revert_map();
    EXPECT(!main_globals.lost_map);tick=800;main_lost_map();main_lost_map_private();
    EXPECT(reverts==1&&!main_globals.loss_timer);
    reset(2);main_lost_map();tick=2147483647L;main_lost_map_private();EXPECT(reverts==1);
    reset(2);tick=2147483600L;main_lost_map();tick=2147483647L;main_lost_map_private();
    EXPECT(main_globals.loss_timer==47);tick=-2147483647L-1;main_lost_map_private();
    EXPECT(!main_globals.loss_timer);tick=-2147483556L;main_lost_map_private();EXPECT(reverts==1);
}
static void coop_blocked(void) {
    int cause,i;
    for(cause=0;cause<5;cause++) {
        reset(2);
        if(cause==0) projectiles=1;
        if(cause==1) enemies=1;
        if(cause==2) airborne=1;
        if(cause==3) vehicle_moving=1;
        if(cause==4) spawn_failed=1;
        main_respawn(FALSE);tick=92;main_respawn_private();EXPECT(main_globals.respawn);
        for(i=0;i<40000;i++) {tick++;main_respawn_private();}
        EXPECT(main_globals.respawn_timer==92&&main_globals.respawn);
        projectiles=enemies=airborne=vehicle_moving=spawn_failed=0;
        main_respawn_private();EXPECT(!main_globals.respawn&&players[1].unit_index!=NONE);
    }
    reset(2);main_respawn(FALSE);players[0].unit_index=NONE;main_lost_map();
    tick=91;main_lost_map_private();main_respawn_private();EXPECT(!reverts&&!spawns);
    tick=92;main_lost_map_private();main_respawn_private();EXPECT(reverts==1&&!spawns);
}
static void cameras(void) {
    struct dead_camera c;
    struct dead_camera_command command;
    struct camera_control control={0,.1f};
    reset(2);memset(&c,0,sizeof(c));memset(&command,0,sizeof(command));
    c.player_index=1;c.current_player_index=NONE;c.unit_index=101;c.switch_timer=0;c.timer=3;
    players[0].unit_index=NONE;
    dead_camera_update(&c,&control,&command);
    EXPECT(c.unit_index==101&&command.position.x==101&&c.switch_timer==3);
    c.switch_timer=0;dead_camera_update(&c,&control,&command);EXPECT(c.unit_index==101);
    players[0].unit_index=100;c.switch_timer=0;
    dead_camera_update(&c,&control,&command);EXPECT(c.unit_index==100&&c.current_player_index==0);
    c.unit_index=NONE;c.position.x=77;c.switch_timer=1;
    dead_camera_update(&c,&control,&command);EXPECT(command.position.x==77);
    c.unit_index=101;c.current_player_index=NONE;c.switch_timer=0;paused=1;
    dead_camera_update(&c,&control,&command);EXPECT(c.unit_index==101);
    paused=0;multiplayer=1;c.switch_timer=0;
    dead_camera_update(&c,&control,&command);EXPECT(c.unit_index==100&&c.switch_timer==15);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("frame-rates") { rates(); return 0; }
	CASE("clock-and-lifecycle") { clock_and_lifecycle(); return 0; }
	CASE("co-op-blocked") { coop_blocked(); return 0; }
	CASE("dead-camera") { cameras(); return 0; }
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
