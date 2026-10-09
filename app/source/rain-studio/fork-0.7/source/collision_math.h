#ifndef FRONTIER_COLLISION_MATH_H
#define FRONTIER_COLLISION_MATH_H
#include <math.h>
typedef int (*GroundHeight)(const float point[3],double *height,void *context);
/* Sample the ray, then bisect its first unsafe interval; no assumed flat floor. */
static double collision_ground_fraction(const float start[3],const float end[3],double clearance,int samples,GroundHeight height,void *context){
 double previous=0;
 for(int step=0;step<=samples;step++){
  double t=(double)step/samples,h;float point[3];
  for(int i=0;i<3;i++)point[i]=(float)(start[i]+(end[i]-start[i])*t);
  if(height(point,&h,context)&&point[1]<h+clearance){
   if(step==0)return 0;
   double low=previous,high=t;
   for(int j=0;j<10;j++){
    double mid=(low+high)/2;
    for(int i=0;i<3;i++)point[i]=(float)(start[i]+(end[i]-start[i])*mid);
    if(height(point,&h,context)&&point[1]<h+clearance)high=mid;else low=mid;
   }
   return low;
  }
  previous=t;
 }
 return 1;
}
static double collision_nearest_limit(double wall,double floor){return fmax(0,fmin(wall,floor));}
static double collision_radius(double current,double allowed,double dt,double response){
 if(allowed<0)allowed=0;
 if(current<0)current=0;
 if(allowed<=current)return allowed;
 if(dt<0)dt=0;
 if(dt>0.05)dt=0.05;
 return current+(allowed-current)*(1-exp(-response*dt));
}
static double collision_fraction(const float start[3],const float end[3],const float hit[3]){
 double denominator=0,numerator=0;
 for(int i=0;i<3;i++){double d=end[i]-start[i];denominator+=d*d;numerator+=(hit[i]-start[i])*d;}
 if(denominator<1e-12)return 0;
 double t=numerator/denominator;return t<0?0:t>1?1:t;
}
static double collision_box_fraction(const float start[3],const float end[3],double xmin,double xmax,double zmin,double zmax){
 double t=1;
 if(start[0]<xmin||start[0]>xmax||start[2]<zmin||start[2]>zmax)return -1;
 if(end[0]<xmin)t=fmin(t,(xmin-start[0])/(end[0]-start[0]));
 if(end[0]>xmax)t=fmin(t,(xmax-start[0])/(end[0]-start[0]));
 if(end[2]<zmin)t=fmin(t,(zmin-start[2])/(end[2]-start[2]));
 if(end[2]>zmax)t=fmin(t,(zmax-start[2])/(end[2]-start[2]));
 return t;
}
#endif
