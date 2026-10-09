#include <assert.h>
#include <stdio.h>
#include "collision_math.h"
static int flat_floor(const float p[3],double *h,void *ctx){(void)p;(void)ctx;*h=0;return 1;}
static int ramp_floor(const float p[3],double *h,void *ctx){(void)ctx;*h=p[0]*.5;return 1;}
static int no_floor(const float p[3],double *h,void *ctx){(void)p;(void)h;(void)ctx;return 0;}
int main(void){
 float groundstart[3]={0,100,0},groundend[3]={100,-100,0};
 double f=collision_ground_fraction(groundstart,groundend,28,24,flat_floor,0);
 assert(f<=.36&&f>.3599);
 f=collision_ground_fraction(groundstart,groundend,28,24,ramp_floor,0);
 assert(f<=.288&&f>.2879);
 assert(collision_ground_fraction(groundstart,groundend,28,24,no_floor,0)==1);
 float above[3]={100,100,0};assert(collision_ground_fraction(groundstart,above,28,24,flat_floor,0)==1);
 float below[3]={0,-10,0};assert(collision_ground_fraction(below,groundend,28,24,flat_floor,0)==0);

 assert(collision_nearest_limit(600,120)==120); // floor closer than wall
 assert(collision_nearest_limit(80,120)==80);   // wall remains the limit
 assert(collision_nearest_limit(600,600)==600);
 assert(collision_radius(600,collision_nearest_limit(600,120),.016,5)==120);
 assert(collision_radius(600,180,.016,5)==180);
 double recovering=collision_radius(180,600,.016,5);assert(recovering>180&&recovering<600);
 double r=180;for(int i=0;i<600;i++){double next=collision_radius(r,600,1.0/60,5);assert(next>=r&&next<=600);r=next;}assert(fabs(r-600)<1e-8);
 assert(collision_radius(600,0,.016,5)==0);
 float start[3]={100,100,100},end[3]={100,-300,100},hit[3]={100,28,100};
 assert(fabs(collision_fraction(start,end,hit)-.18)<1e-7);
 float wallend[3]={700,100,100},wallhit[3]={250,100,100};assert(fabs(collision_fraction(start,wallend,wallhit)-.25)<1e-9);
 assert(collision_box_fraction(start,wallend,28,400,28,400)==.5);
 float diagonal[3]={700,100,1300};assert(fabs(collision_box_fraction(start,diagonal,28,400,28,400)-.25)<1e-9);
 float outside[3]={0,100,100};assert(collision_box_fraction(outside,end,28,400,28,400)==-1);
 assert(collision_fraction(start,start,start)==0);
 puts("PASS: immediate inward clipping, smooth outward recovery, floor/wall projection, map boundaries and degeneracy.");return 0;
}
