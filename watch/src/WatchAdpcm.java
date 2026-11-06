package cn.sonic.voicestick.watch;

final class WatchAdpcm {
    static final int[] STEP={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
    static final int[] ADJUST={-1,-1,-1,-1,2,4,6,8};
    static byte[] encode(byte[] pcm,int length) {
        int samples=length/2, value=(short)((pcm[0]&255)|(pcm[1]<<8)), index=32;
        byte[] encoded=new byte[4+samples/2];
        encoded[0]=pcm[0];encoded[1]=pcm[1];encoded[2]=(byte)samples;encoded[3]=(byte)(samples>>8);
        for(int i=1;i<samples;i++){
            int sample=(short)((pcm[i*2]&255)|(pcm[i*2+1]<<8)),delta=sample-value;
            int code=delta<0?8:0,step=STEP[index],diff=step>>3;
            delta=Math.abs(delta);
            for(int bit=4;bit>0;bit>>=1,step>>=1)if(delta>=step){code|=bit;delta-=step;diff+=step;}
            value=Math.max(-32768,Math.min(32767,value+((code&8)==0?diff:-diff)));
            index=Math.max(0,Math.min(88,index+ADJUST[code&7]));
            encoded[4+(i-1)/2]|=(byte)(code<<(4*((i-1)%2)));
        }
        return encoded;
    }
}
