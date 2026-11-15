#include "aod_postprocess.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define AOD_PREPARE_MAX_PIXELS ((size_t)4194304)

static int overlap(const void *a, size_t an, const void *b, size_t bn) {
    if (!a || !b || !an || !bn) return 0;
    uintptr_t x=(uintptr_t)a, y=(uintptr_t)b;
    return x<=y ? y-x<an : x-y<bn;
}

/* Label nonzero pixels in scan order. Reuse the queue for each component. */
static uint32_t largest(const uint8_t *src, int w, int h, int threshold,
                        int diagonal, uint32_t *labels, uint32_t *queue,
                        size_t *best_size) {
    size_t n=(size_t)w*(size_t)h;
    uint32_t id=0, best=0;
    *best_size=0;
    memset(labels,0,n*sizeof(*labels));
    for (size_t i=0;i<n;i++) {
        if (labels[i] || src[i]<=threshold) continue;
        ++id;
        size_t head=0,tail=0;
        labels[i]=id; queue[tail++]=(uint32_t)i;
        while (head<tail) {
            uint32_t cur=queue[head++]; int x=(int)(cur%(uint32_t)w), y=(int)(cur/(uint32_t)w);
            for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
                if ((!dx&&!dy) || (!diagonal && dx && dy)) continue;
                int nx=x+dx,ny=y+dy;
                if (nx<0||ny<0||nx>=w||ny>=h) continue;
                size_t j=(size_t)ny*(size_t)w+(size_t)nx;
                if (!labels[j] && src[j]>threshold) { labels[j]=id; queue[tail++]=(uint32_t)j; }
            }
        }
        if (tail>*best_size) { *best_size=tail; best=id; }
    }
    return best;
}

int aod_prepare_mask(const uint8_t *soft, size_t length, int width, int height,
                     uint8_t *binary, size_t binary_length) {
    if (!binary || binary_length>AOD_PREPARE_MAX_PIXELS || overlap(soft,length,binary,binary_length)) return -1;
    memset(binary,0,binary_length);
    if (!soft || width<=0 || height<=0 || (size_t)width>SIZE_MAX/(size_t)height) return -1;
    size_t n=(size_t)width*(size_t)height;
    if (n>AOD_PREPARE_MAX_PIXELS || length!=n || binary_length!=n) return -1;
    uint8_t *mask=malloc(n), *fg=malloc(n);
    uint32_t *labels=malloc(n*sizeof(*labels)), *queue=malloc(n*sizeof(*queue));
    if (!mask||!fg||!labels||!queue) { free(mask);free(fg);free(labels);free(queue);return -1; }
    size_t area=0;
    uint32_t best=largest(soft,width,height,0,0,labels,queue,&area);
    if (!best) goto fail;
    uint64_t sum=0;
    for (size_t i=0;i<n;i++) { mask[i]=labels[i]==best?soft[i]:0; sum+=mask[i]; }
    if ((double)sum/(double)area<128.0) goto fail;
    best=largest(mask,width,height,128,1,labels,queue,&area);
    if (!best || area<(size_t)((double)n*0.20000000298023224)) goto fail;
    for (size_t i=0;i<n;i++) fg[i]=labels[i]==best?255:0;
    /* Mark 8-connected background reachable from every image edge. */
    memset(labels,0,n*sizeof(*labels)); size_t head=0,tail=0;
    for (size_t i=0;i<n;i++) {
        size_t x=i%(size_t)width,y=i/(size_t)width;
        if ((x==0||y==0||x==(size_t)width-1||y==(size_t)height-1) && !fg[i] && !labels[i]) {
            labels[i]=1; queue[tail++]=(uint32_t)i;
        }
    }
    while (head<tail) {
        uint32_t cur=queue[head++]; int x=(int)(cur%(uint32_t)width),y=(int)(cur/(uint32_t)width);
        for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
            if (!dx&&!dy) continue;
            int nx=x+dx,ny=y+dy;
            if (nx<0||ny<0||nx>=width||ny>=height) continue;
            size_t j=(size_t)ny*(size_t)width+(size_t)nx;
            if (!fg[j]&&!labels[j]) { labels[j]=1;queue[tail++]=(uint32_t)j; }
        }
    }
    for (size_t i=0;i<n;i++) {
        if (!labels[i] && mask[i]<=199) mask[i]=255;
        binary[i]=mask[i]>40?255:0;
    }
    free(mask);free(fg);free(labels);free(queue);return 0;
fail:
    free(mask);free(fg);free(labels);free(queue);return -1;
}

static int reflect101(int i, int n) {
    if (n==1) return 0;
    while (i<0 || i>=n) i=i<0?-i:2*n-i-2;
    return i;
}

int aod_finish_mask(const uint8_t *binary, size_t length, int width, int height,
                    uint8_t *alpha, size_t alpha_length, int out_width, int out_height) {
    const size_t max_out=(size_t)16777216;
    if (!alpha || alpha_length>max_out || overlap(binary,length,alpha,alpha_length)) return -1;
    memset(alpha,0,alpha_length);
    if (!binary || width<=0 || height<=0 || out_width<=0 || out_height<=0 ||
        (size_t)width>SIZE_MAX/(size_t)height || (size_t)out_width>SIZE_MAX/(size_t)out_height) return -1;
    size_t n=(size_t)width*(size_t)height, out_n=(size_t)out_width*(size_t)out_height;
    if (n>AOD_PREPARE_MAX_PIXELS || out_n>max_out || n!=length || out_n!=alpha_length) return -1;
    uint8_t *a=malloc(out_n),*b=malloc(out_n);
    if (!a||!b) { free(a);free(b);return -1; }
    /* OpenCV's 8-bit linear interpolation uses 11-bit fixed coefficients. */
    for (int y=0;y<out_height;y++) {
        float fy=(float)(((double)y+0.5)*height/out_height-0.5);
        int base_y=(int)floorf(fy); fy-=base_y;
        int y0=base_y<0?0:(base_y>=height?height-1:base_y);
        int next_y=base_y+1;
        int y1=next_y<0?0:(next_y>=height?height-1:next_y);
        int by=(int)lrintf(fy*2048.f), ay=(int)lrintf((1.f-fy)*2048.f);
        for (int x=0;x<out_width;x++) {
            float fx=(float)(((double)x+0.5)*width/out_width-0.5);
            int x0=(int)floorf(fx); fx-=x0;
            if (x0<0) { x0=0;fx=0; } else if (x0>=width-1) { x0=width-1;fx=0; }
            int x1=x0+1<width?x0+1:x0;
            int bx=(int)lrintf(fx*2048.f), ax=(int)lrintf((1.f-fx)*2048.f);
            unsigned top=binary[(size_t)y0*width+x0]*(unsigned)ax+binary[(size_t)y0*width+x1]*(unsigned)bx;
            unsigned bot=binary[(size_t)y1*width+x0]*(unsigned)ax+binary[(size_t)y1*width+x1]*(unsigned)bx;
            /* OpenCV 4.x 8-bit vertical resize reduces intermediate precision;
             * see modules/imgproc/src/resize.cpp, VResizeLinear. */
            a[(size_t)y*out_width+x]=(uint8_t)((((unsigned)ay*(top>>4)>>16)+
                                                   ((unsigned)by*(bot>>4)>>16)+2)>>2);
        }
    }
    /* 5x5 ELLIPSE, default anchor and a constant 255 border. */
    for (int iteration=0;iteration<3;iteration++) {
        for(int y=0;y<out_height;y++) for(int x=0;x<out_width;x++) {
            uint8_t v=255;
            for(int dy=-2;dy<=2;dy++) for(int dx=-2;dx<=2;dx++) {
                if ((dy==-2||dy==2)&&dx) continue;
                int xx=x+dx, yy=y+dy;
                if(xx>=0&&yy>=0&&xx<out_width&&yy<out_height && a[(size_t)yy*out_width+xx]<v)
                    v=a[(size_t)yy*out_width+xx];
            }
            b[(size_t)y*out_width+x]=v;
        }
        uint8_t *tmp=a;a=b;b=tmp;
    }
    const unsigned k[5]={1,4,6,4,1};
    for(int y=0;y<out_height;y++) for(int x=0;x<out_width;x++) {
        unsigned sum=0;
        for(int dy=-2;dy<=2;dy++) for(int dx=-2;dx<=2;dx++)
            sum+=a[(size_t)reflect101(y+dy,out_height)*out_width+reflect101(x+dx,out_width)]*k[dy+2]*k[dx+2];
        alpha[(size_t)y*out_width+x]=(uint8_t)((sum+128)>>8);
    }
    free(a);free(b);return 0;
}
