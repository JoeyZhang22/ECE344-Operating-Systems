#include <assert.h>
#include "common.h"
#include "point.h"
#include "math.h"

void
point_translate(struct point *p, double x, double y)
{
	p->x+=x;
	p->y+=y;
}

double
point_distance(const struct point *p1, const struct point *p2)
{	
	double x1x2= p1->x - p2->x;
	double y1y2= p1->y - p2->y;
	double distance = sqrt(pow(x1x2,2)+(pow(y1y2,2)));
	return distance;
}

int
point_compare(const struct point *p1, const struct point *p2)
{
	double distance1= sqrt(pow(p1->x,2)+pow(p1->y,2));
	double distance2= sqrt(pow(p2->x,2)+pow(p2->y,2));
	if(distance1<distance2){
		return -1;
	}
	else if(distance1==distance2){
		return 0;
	}
	else{
		return 1;
	}
	return 0;
}
