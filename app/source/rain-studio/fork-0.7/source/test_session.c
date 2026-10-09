#include <assert.h>
#include <stdio.h>
#include "orbit_session.h"
int main(void){
 OrbitSession s={0};float p[3]={0,0,0};
 assert(!orbit_session_ready(&s,1000,1,2,3,4,p,0));
 assert(!orbit_session_ready(&s,1499,1,2,3,4,p,0));
 assert(orbit_session_ready(&s,1500,1,2,3,4,p,0));
 assert(!orbit_session_calibrate(&s,20,150,1));
 assert(orbit_session_calibrate(&s,600,150,1));s.pending=0;
 assert(orbit_session_ready(&s,1516,1,2,3,4,p,0));
 // Same actor allocation survives a map transition, with a short transient camera.
 assert(!orbit_session_ready(&s,1532,1,20,30,40,p,0));
 assert(orbit_session_calibrate(&s,90,-400,1));
 assert(s.radius==600&&s.height==150);
 assert(orbit_session_ready(&s,2032,1,20,30,40,p,0));s.pending=0;
 // A frame stall must not permanently recapture collision-shortened distance.
 assert(!orbit_session_ready(&s,3000,1,20,30,40,p,0));
 assert(orbit_session_calibrate(&s,85,5,1));assert(s.radius==600);
 assert(orbit_session_ready(&s,3500,1,20,30,40,p,0));s.pending=0;
 // Teleport with reused pointers; ordinary movement remains continuous.
 p[0]=10;assert(orbit_session_ready(&s,3516,1,20,30,40,p,0));
 p[0]=2000;assert(!orbit_session_ready(&s,3532,1,20,30,40,p,0));
 assert(orbit_session_ready(&s,4032,1,20,30,40,p,0));s.pending=0;
 // Toggle and replacement actor both settle, retaining the trusted reference.
 assert(!orbit_session_ready(&s,4048,1,20,30,40,p,1));
 assert(orbit_session_ready(&s,4548,1,20,30,40,p,1));s.pending=0;
 assert(!orbit_session_ready(&s,4564,99,20,30,40,p,1));
 orbit_session_suspend(&s);assert(!orbit_session_ready(&s,4700,99,20,30,40,p,1));
 assert(s.radius==600&&s.height==150);
 puts("PASS: transition settling, short-eye regression, stalls, reused actors, teleports, toggles and reference retention.");
}
