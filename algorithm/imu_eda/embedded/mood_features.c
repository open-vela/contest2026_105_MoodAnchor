#include "mood_features.h"
#include <math.h>

static float mean(const float *x, unsigned int n) { float s=0.0f; for(unsigned int i=0;i<n;i++) s+=x[i]; return s/n; }
static float stdev(const float *x, unsigned int n, float m) { float s=0.0f; for(unsigned int i=0;i<n;i++){float d=x[i]-m;s+=d*d;} return sqrtf(s/n); }
static void sort(float *x, unsigned int n) { for(unsigned int i=1;i<n;i++){float v=x[i];unsigned int j=i;while(j&&x[j-1]>v){x[j]=x[j-1];j--;}x[j]=v;} }

bool mood_bvp_features(const float x[MOOD_BVP_WINDOW_SAMPLES], float out[7]) {
    float smooth[MOOD_BVP_WINDOW_SAMPLES], ordered[MOOD_BVP_WINDOW_SAMPLES], ibi[14], hr[14];
    unsigned int peaks[15], peak_count=0, ibi_count=0;
    for(unsigned int i=0;i<MOOD_BVP_WINDOW_SAMPLES;i++) { float s=0.0f; unsigned int c=0; for(int k=-2;k<=2;k++){int j=(int)i+k;if(j>=0&&j<MOOD_BVP_WINDOW_SAMPLES){s+=x[j];c++;}} smooth[i]=s/c; ordered[i]=smooth[i]; }
    sort(ordered,MOOD_BVP_WINDOW_SAMPLES); const float threshold=ordered[(unsigned int)(.65f*(MOOD_BVP_WINDOW_SAMPLES-1))];
    for(unsigned int i=1;i<MOOD_BVP_WINDOW_SAMPLES-1;i++) if(smooth[i]>smooth[i-1]&&smooth[i]>=smooth[i+1]&&smooth[i]>=threshold) { if(!peak_count||i-peaks[peak_count-1]>=22) peaks[peak_count++]=i; else if(smooth[i]>smooth[peaks[peak_count-1]]) peaks[peak_count-1]=i; }
    if(peak_count<3) return false;
    for(unsigned int i=1;i<peak_count;i++){ibi[ibi_count]=(float)(peaks[i]-peaks[i-1])/64.0f;hr[ibi_count]=60.0f/ibi[ibi_count];ibi_count++;}
    float xm=mean(x,MOOD_BVP_WINDOW_SAMPLES), hs=stdev(hr,ibi_count,mean(hr,ibi_count)), im=mean(ibi,ibi_count), is=stdev(ibi,ibi_count,im), energy=0.0f, sq=0.0f;
    for(unsigned int i=0;i<MOOD_BVP_WINDOW_SAMPLES;i++) energy+=x[i]*x[i];
    for(unsigned int i=1;i<ibi_count;i++){float d=ibi[i]-ibi[i-1];sq+=d*d;}
    out[0]=xm;out[1]=stdev(x,MOOD_BVP_WINDOW_SAMPLES,xm);out[2]=energy/MOOD_BVP_WINDOW_SAMPLES;out[3]=mean(hr,ibi_count);out[4]=hs;out[5]=is;out[6]=(ibi_count>1)?sqrtf(sq/(ibi_count-1)):0.0f; return true;
}

void mood_eda_features(const float x[MOOD_EDA_WINDOW_SAMPLES], float out[8]) {
    float d[MOOD_EDA_WINDOW_SAMPLES-1], m=mean(x,MOOD_EDA_WINDOW_SAMPLES), ad=0.0f, range=x[0], low=x[0], rise=0.0f, det2=0.0f;
    for(unsigned int i=1;i<MOOD_EDA_WINDOW_SAMPLES;i++){d[i-1]=(x[i]-x[i-1])*4.0f;ad+=fabsf(d[i-1]);if(x[i]>range)range=x[i];if(x[i]<low)low=x[i];}
    float dm=mean(d,MOOD_EDA_WINDOW_SAMPLES-1), ds=stdev(d,MOOD_EDA_WINDOW_SAMPLES-1,dm);for(unsigned int i=0;i<MOOD_EDA_WINDOW_SAMPLES-1;i++)if(d[i]>ds)rise+=1.0f;
    for(unsigned int i=0;i<MOOD_EDA_WINDOW_SAMPLES;i++){float trend=x[0]+(x[MOOD_EDA_WINDOW_SAMPLES-1]-x[0])*(float)i/(MOOD_EDA_WINDOW_SAMPLES-1);float z=x[i]-trend;det2+=z*z;}
    out[0]=m;out[1]=stdev(x,MOOD_EDA_WINDOW_SAMPLES,m);out[2]=range-low;out[3]=(x[MOOD_EDA_WINDOW_SAMPLES-1]-x[0])/5.0f;out[4]=ad/(MOOD_EDA_WINDOW_SAMPLES-1);out[5]=ds;out[6]=rise/(MOOD_EDA_WINDOW_SAMPLES-1);out[7]=sqrtf(det2/MOOD_EDA_WINDOW_SAMPLES);
}

