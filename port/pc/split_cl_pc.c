/* QuakeOG: the split game's PC client half (pc/CMakeLists.txt, quakeog_cl).
 * qog_manifest and qog_tour read the server's state directly, which the
 * client half does not have, so they are single-process tools only. */
void qog_manifest_register(void) {}
void qog_tour_register(void) {}
void qog_tour_frame(void) {}
/* qog_fly and qog_fly_ref move the server's player, also single-process only */
typedef struct quakeparms_s quakeparms_t;
void qog_fly_register(void) {}
void qog_fly_frame(void) {}
void qog_fly_pc_main(quakeparms_t *parms) { (void)parms; }
