/***************************************************************************/
/*                               micro-Max,                                */
/* A chess program smaller than 2KB (of non-blank source), by H.G. Muller  */
/***************************************************************************/
/* version 1.6 (1433 non-blank characters) features:                       */
/* - recursive negamax search                                              */
/* - quiescence search with recaptures                                     */
/* - recapture extensions                                                  */
/* - (internal) iterative deepening                                        */
/* - best-move-first 'sorting'                                             */
/* - full FIDE rules and move-legality checking                            */

/* accepts under-promotions: type 1,2,3 (=R,B,N) after input move          */
/* (input buffer c[] & *P made global, K and N encoding swapped for this)  */

void OUTCH(char);
char INCH(void);
int RND(void);
void SETATTR(char);             /* set the color/attribute latch (reverse/color) */
void CLS(void);                 /* clear screen + home (strobe redraw)        */
#define W while

int M=136,S=128,I=8000,C=799,Q,O,K,N,NODES=3000,HB;  /* NODES=difficulty; HB=human plays Black */
int pf=-1,pt,cf=-1,ct;          /* last move played: (p)layer / (c)omputer, -1=none */

char L,*P,
w[]={0,1,1,-1,3,3,5,9},                      
o[]={-16,-15,-17,0,1,16,0,1,16,15,17,0,14,18,31,33,0,
     7,-1,6,11,8,3,6,                          
     6,4,5,7,3,5,4,6},                         
b[129],

n[]=".?+knbrq?*?KNBRQ",

c[9];

D(k,q,l,e,E,z,n)        
int k,q,l,e,E,z,n;      
{                       
 int j,r,m,v,d,h,i,F,G,s;
 char t,p,u,x,y,X,Y,H,B;

 q--;                                          
 d=X=Y=0;                                      

 W(d++<n||                                     
   z==8&K==I&&(N<NODES&d<98||                    
   (K=X,L=Y&~M,d=2)))                          
 {x=B=X;                                       
  h=Y&S;                                   
  m=d>1?-I:e;                                  
  N++;                                         
  do{u=b[x];                                   
   if(u&k)                                     
   {r=p=u&7;                                   
    j=o[p+16];                                 
    W(r=p>2&r<0?-r:-o[++j])                    
    {A:                                        
     y=x;F=G=S;                                
     do{                                       
      H=y=h?Y^h:y+r;                        
      if(y&M)break;                            
      m=E-S&&b[E]&&y-E<2&E-y<2?I:m;    /* castling-on-Pawn-check bug fixed */
      if(p<3&y==E)H^=16;                       
      t=b[H];if(t&k|p<3&!(y-x&7)-!t)break;       
      i=99*w[t&7];                             
      m=i<0?I:m;                       /* castling-on-Pawn-check bug fixed */
      if(m>=l)goto C;                          

      if(s=d-(y!=z))                           
      {v=p<6?b[x+8]-b[y+8]:0;
       b[G]=b[H]=b[x]=0;b[y]=u|32;             
       if(!(G&M))b[F]=k+6,v+=30;               
       if(p<3)                                 
       {v-=9*((x-2&M||b[x-2]-u)+               
              (x+2&M||b[x+2]-u)-1);            
        if(y+r+1&S)b[y]|=7,i+=C;               
       }
       v=-D(24-k,-l,m>q?-m:-q,-e-v-i,F,y,s);   
       if(K-I)                                 
       {if(v+I&&x==K&y==L&z==8)                
        {Q=-e-i;O=F;
         if(b[y]-u&7&&P-c>5)b[y]-=c[4]&3;        /* under-promotions */
         return l;
        }v=m;                                   
       }                                       
       b[G]=k+6;b[F]=b[y]=0;b[x]=u;b[H]=t;     
       if(v>m)                         
        m=v,X=x,Y=y|S&F;                       
       if(h){h=0;goto A;}                            
      }
      if(x+r-y|u&32|                           
         p>2&(p-3|j-7||                        
         b[G=x+3^r>>1&7]-k-6                   
         ||b[G^1]|b[G^2])                      
        )t+=p<5;                               
      else F=y;                                
     }W(!t);                                   
  }}}W((x=x+9&~M)-B);                          
C:if(m>I-M|m<M-I)d=98;                         
  m=m+I?m:-D(24-k,-I,I,0,S,S,1);    
 }                                             
 return m+=m<e;                                
}

PUTS(s)char*s;{W(*s)OUTCH(*s++);}                   /* print a string          */

INIT()                          /* set up the opening position                */
{K=8;W(K--)
 {b[K]=(b[K+112]=o[K+24]+8)+8;b[K+16]=18;b[K+96]=9;
  L=8;W(L--)b[16*L+K+8]=(K-4)*(K-4)+(2*L-7)*(2*L-7)/4;
 }
}

SQ(s)int s;{OUTCH(97+(s&7));OUTCH(56-(s>>4));}      /* 0x88 square -> "e4"      */

PROBE(s)int s;                  /* shallow 2-ply score for side s, WITHOUT      */
{int r,nd;                      /* moving: K!=I so the engine never executes.   */
 nd=NODES;NODES=1;K=2000;N=0;   /* NODES=1 -> base 2-ply only (cheap)           */
 r=D(s,-I,I,Q,O,8,2);           /* ~ -I if s has no legal move; ~ +I if s can   */
 NODES=nd;return r;             /* capture the enemy king (i.e. enemy in check) */
}

SHOW()                          /* strobe: clear + draw a framed, labeled board */
{int r,f,rr,ff,p;char*s="..PKNBRQ.P.KNBRQ";         /* uppercase piece letters */
 CLS();
 SETATTR(2);                    /* normal = green on black (baseline for the board) */
 PUTS("        MFC CHESS v1.0\n\n");
 PUTS("      +-----------------+\n");
 r=0;W(r<8)                                         /* display rows top->bottom*/
 {rr=HB?7-r:r;                                      /* flip board if Black     */
  PUTS("    ");OUTCH(HB?49+r:56-r);PUTS(" | ");      /* rank label + left frame */
  f=0;W(f<8)
  {ff=HB?7-f:f;
   p=b[16*rr+ff]&15;
   if(s[p]==46)OUTCH(46);                           /* empty square            */
   else if(p>8){SETATTR(0x82);OUTCH(s[p]);SETATTR(2);} /* White -> reverse video */
   else OUTCH(s[p]);                                /* Black -> normal         */
   OUTCH(32);f++;
  }
  PUTS("| ");                                       /* right frame             */
  if(r==0)PUTS(" computer");                         /* opponent label, at top  */
  if(r==1&&cf>=0){OUTCH(32);SQ(cf);SQ(ct);}          /* its last move, beneath  */
  if(r==6)PUTS(" player");                            /* your label              */
  if(r==7&&pf>=0){OUTCH(32);SQ(pf);SQ(pt);}          /* your last move, beneath */
  OUTCH(10);r++;
 }
 PUTS("      +-----------------+\n");
 PUTS("        ");                                   /* file labels             */
 f=0;W(f<8){OUTCH(HB?104-f:97+f);OUTCH(32);f++;}
 OUTCH(10);
}

main()
{int k,m,hs;                    /* hs = the human's side value (8=White,16=Black) */

 W(1)                                               /* splash / new-game menu  */
 {CLS();
  PUTS("\n\n      MFC CHESS v1.0\n\n");
  PUTS("   a port of micro-Max\n   by H.G. Muller\n\n\n");
  PUTS("   (N)ew game     (Q)uit\n");
  W(INCH()!='n');                                   /* N starts; Q/ESC -> DOS  */

  CLS();PUTS("\n  Play (W)hite or (B)lack? ");
  do m=INCH();W(m!='w'&&m!='b');HB=m=='b';

  CLS();PUTS("\n  Skill (E)asy (M)edium (H)ard? ");
  do m=INCH();W(m!='e'&&m!='m'&&m!='h');
  NODES=m=='e'?400:m=='h'?6000:1500;                /* difficulty -> node budget*/

  INIT();k=8;pf=cf=-1;hs=HB?16:8;                   /* fresh game, White first */

  W(1)                                              /* one half-move per pass  */
  {SHOW();
   if(PROBE(k)<M-I)                                 /* side to move has no move*/
   {PUTS(PROBE(24-k)>I-M                            /* enemy can grab the king?*/
         ?(k==hs?"\n      *** Checkmate - Try Again ***"  /* you are mated     */
                :"\n      *** Checkmate - You Win! ***")  /* cpu is mated       */
         :"\n      *** Stalemate - Draw ***");            /* no check -> draw   */
    PUTS("\n\n  Press a key for the menu...");INCH();break;
   }
   if(k!=hs)                                        /* the computer's turn     */
   {PUTS("\nthinking...");K=I;N=0;
    if(D(k,-I,I,Q,O,8,2)==I){k^=24;cf=K;ct=L;}
    continue;
   }
   PUTS("\nYour move: ");                            /* your turn               */
   P=c;                                             /* read a line; the move   */
   W(1)                                             /* submits ONLY on RETURN  */
   {m=INCH();
    if(m==8){if(P>c){P--;OUTCH(8);}continue;}       /* backspace edits in place*/
    if(m==10)break;                                 /* RETURN ends the move    */
    if(P-c<8)*P++=m;                                /* store (ignore overflow) */
   }
   *P++=10;                                         /* terminate like a line   */
   K=I;
   if(*c-10)                                        /* a move was typed        */
   {K=*c-16*c[1]+C;L=c[2]-16*c[3]+C;
    N=0;
    if(D(k,-I,I,Q,O,8,2)==I){k^=24;pf=K;pt=L;}      /* legal -> play & record  */
   }                                                /* illegal -> just reprompt*/
  }
 }
}

