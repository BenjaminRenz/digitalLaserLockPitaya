//adapted from http://www.kurims.kyoto-u.ac.jp/~ooura/fft.html
//modified to work for floats
//removed all functions except cdft


void cdft(int n, int isgn, float* a, int* ip, float* w);
//int n          twice dimension of fft =(2^somepower) = len(a)
//int isgn       +1 for forward fft, -1 for ifft (sign inside the exponent e^(isgn*i*...))
//float a*      data in and out pointer, is transformed in place so get's overwritten data format (Re0,Im0,Re1,Im1,Re2,Im2, ... ReN,ImN) -> len(a)=2*(2^somepower)
//int *ip        pointer to a temporary working area for the bit reversal process, for init ip=malloc((2+sqrt(n))*sizeof(int));ip[0]=0;
//float *w      pointer to a temporary working area for cos sin lookup tables, for init w=malloc(n/2*sizeof(double));ip[0]=0;
//ip and w can be reused for any subsequent calls

//Alternativeley directly initialize ip and w
void makewt(int nw, int *ip, float* w);
//nw=n>>2
//so call like this:   makewt(n >> 2, ip, w);
