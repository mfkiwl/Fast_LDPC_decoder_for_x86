/**
  Copyright (c) 2012-2015 "Bordeaux INP, Bertrand LE GAL"
  [http://legal.vvv.enseirb-matmeca.fr]

  This file is part of LDPC_C_Simulator.

  LDPC_C_Simulator is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "CDecoder_OMS_fixed_AVX.h"
#include "../../CTools/transpose_avx.h"
#include "../../CTools/CTools.h"

#define TYPE __m256i

#define VECTOR_LOAD(ptr)            (_mm256_load_si256(ptr))
#define VECTOR_UNCACHED_LOAD(ptr)   (_mm256_stream_load_si256(ptr))
#define VECTOR_STORE(ptr,v)         (_mm256_store_si256(ptr,v))
#define VECTOR_ADD(a,b)             (_mm256_adds_epi8(a,b))
#define VECTOR_SBU(a,b)             (_mm256_subs_epu8(a,b))
#define VECTOR_SUB(a,b)             (_mm256_subs_epi8(a,b))
#define VECTOR_ABS(a)               (_mm256_abs_epi8(a))
#define VECTOR_MAX(a,b)             (_mm256_max_epi8(a,b))
#define VECTOR_MIN(a,b)             (_mm256_min_epi8(a,b))
#define VECTOR_XOR(a,b)             (_mm256_xor_si256(a,b))
#define VECTOR_OR(a,b)              (_mm256_or_si256(a,b))
#define VECTOR_AND(a,b)             (_mm256_and_si256(a,b))
#define VECTOR_ANDNOT(a,b)          (_mm256_andnot_si256(a,b))
#define VECTOR_MIN_1(a,min1)        (VECTOR_MIN(a,min1))
#define VECTOR_SIGN(a,b)            (_mm256_sign_epi8(a,b))
#define VECTOR_EQUAL(a,b)           (_mm256_cmpeq_epi8(a,b))
#define VECTOR_ZERO                 (_mm256_setzero_si256())
#define VECTOR_SET1(a)              (_mm256_set1_epi8(a))

#define VECTOR_MIN_2(val,old_min1,min2) \
    (VECTOR_MIN(min2,VECTOR_MAX(val,old_min1)))

#define VECTOR_VAR_SATURATE(a) \
    (VECTOR_MAX(VECTOR_MIN(a, max_var), min_var))

#define VECTOR_SATURATE(a, max, min) \
    (VECTOR_MAX(VECTOR_MIN(a, max), min))

#define VECTOR_SUB_AND_SATURATE_VAR_8bits(a,b,min) \
    (VECTOR_MAX(VECTOR_SUB(a,b), min)) // ON DOIT CONSERVER LA SATURATION MIN A CAUSE DE -128

#define VECTOR_ADD_AND_SATURATE_VAR_8bits(a,b,min) \
    (VECTOR_MAX(VECTOR_ADD(a,b), min)) // ON DOIT CONSERVER LA SATURATION MIN A CAUSE DE -128

#define VECTOR_SUB_AND_SATURATE_VAR(a,b,max,min) \
    (VECTOR_SATURATE(VECTOR_SUB(a,b),max,min))

#define VECTOR_ADD_AND_SATURATE_VAR(a,b,max,min) \
    (VECTOR_SATURATE(VECTOR_ADD(a,b),max,min))

#define VECTOR_invSIGN2(val,sig) \
    (VECTOR_SIGN(val, sig))


inline TYPE VECTOR_GET_SIGN_BIT(TYPE a, TYPE m){
    TYPE b = VECTOR_AND(a, m);
    return b;
}


inline TYPE VECTOR_CMOV( TYPE a, TYPE b, TYPE c, TYPE d){
    TYPE z = VECTOR_EQUAL  ( a, b );
//    return _mm256_blendv_epi8( d, c, z ); SEEMS TO BE LESS EFFIENT :-(
    TYPE g = VECTOR_AND   ( c, z );
    TYPE h = VECTOR_ANDNOT( z, d );
    return VECTOR_OR      ( g, h );
}


inline int VECTOR_XOR_REDUCE(TYPE reg){
//    unsigned int *p;
//    p = (unsigned int*)&reg;
//    return ((p[0] | p[1] | p[2] | p[3] | p[4] | p[5] | p[6] | p[7]) != 0);
    const TYPE mask = VECTOR_SET1( -128 );
    reg = VECTOR_AND(reg, mask);
    unsigned int *p;
    p = (unsigned int*)&reg;
    return (( p[0] | p[1] | p[2]| p[3] | p[4] | p[5] | p[6] | p[7] )) != 0;
}

#define PETIT 1
#define MANUAL_PREFETCH 1

CDecoder_OMS_fixed_AVX::CDecoder_OMS_fixed_AVX()
{
    nb_exec         = 0;
    nb_saved_iters  = 0;
    offset          = -1;

//    for(int i=0; i<_M; i++){
//        p_vn_adr[i] = NULL;
//    }
    
#if PETIT == 1
    p_vn_adr        = new __m256i *[_M];
    for(int i=0; i<_M; i++){
        p_vn_adr[i] = &var_nodes[ PosNoeudsVariable[i] ];
    }
#endif
}


#define VERBOSE 0
CDecoder_OMS_fixed_AVX::~CDecoder_OMS_fixed_AVX()
{
#if VERBOSE == 1
    float sav = (float)nb_saved_iters/(float)nb_exec;
    printf("(DD) # SAVED ITERS / EXEC = %f\n", sav);
#endif
#if PETIT == 1
    delete p_vn_adr;
#endif
 
}


void CDecoder_OMS_fixed_AVX::setOffset(int _offset)
{
    if( offset == -1 ){
        offset = _offset;
    }else{
        printf("(EE) Offset value was already configured (%d)\n", offset);
        exit( 0 );
    }
}


void CDecoder_OMS_fixed_AVX::decode(char Intrinsic_fix[], char Rprime_fix[], int nombre_iterations)
{
    if( vSAT_POS_VAR == 127 )
        decode_8bits(Intrinsic_fix, Rprime_fix, nombre_iterations);
    else
        exit( 0 );//decode_generic(Intrinsic_fix, Rprime_fix, nombre_iterations);
    nb_exec += 1;
}


bool CDecoder_OMS_fixed_AVX::decode_8bits(char Intrinsic_fix[], char Rprime_fix[], int nombre_iterations)
{
    ////////////////////////////////////////////////////////////////////////////
    //
    // Initilisation des espaces memoire
    //
    const TYPE zero = VECTOR_ZERO;
    for (int i=0; i<MESSAGE; i++){
        var_mesgs[i] = zero;
    }
    //
    ////////////////////////////////////////////////////////////////////////////

    
    ////////////////////////////////////////////////////////////////////////////
    //
    // ENTRELACEMENT DES DONNEES D'ENTREE POUR POUVOIR EXPLOITER LE MODE SIMD
    //
    if( NOEUD%32 == 0  ){
        uchar_transpose_avx((TYPE*)Intrinsic_fix, (TYPE*)var_nodes, NOEUD);
    }else{
        char *ptrVar = (char*)var_nodes;
        for (int i=0; i<NOEUD; i++){
            for (int z=0; z<32; z++){
                ptrVar[32 * i + z] = Intrinsic_fix[z * NOEUD + i];
            }
        }
    }
    //
    ////////////////////////////////////////////////////////////////////////////
    
//    unsigned int arret;

    while (nombre_iterations--) {                           // main loop
        TYPE *p_msg1r                       = var_mesgs;    // read pointer to msg
        TYPE *p_msg1w                       = var_mesgs;    // write pointer to msg
#if PETIT == 1
        __m256i **p_indice_nod1 = p_vn_adr;                 // pointer to vn adr matrix
        __m256i **p_indice_nod2 = p_vn_adr;
#else
        const unsigned short *p_indice_nod1 = PosNoeudsVariable;
        const unsigned short *p_indice_nod2 = PosNoeudsVariable;
#endif
//        arret = 0;

        const TYPE min_var = VECTOR_SET1( vSAT_NEG_VAR );   // set min En -(2^7-1)
        const TYPE max_msg = VECTOR_SET1( vSAT_POS_MSG );   // set max check msg (2^5-1)

        for (int i=0; i<DEG_1_COMPUTATIONS; i++){           // check msg update loop1
//IACA_START
            
            TYPE tab_vContr[DEG_1];                         // variable msg buff
            TYPE sign = VECTOR_ZERO;
            TYPE min1 = VECTOR_SET1(vSAT_POS_VAR);          // min
            TYPE min2 = min1;                               // submin

#if (DEG_1 & 0x01) == 1
        const unsigned char sign8   = 0x80;                 // 1000 0000 B
        const unsigned char isign8  = 0xC0;                 // 1100 0000 B
        const TYPE msign8        = VECTOR_SET1( sign8   );
        const TYPE misign8       = VECTOR_SET1( isign8  );
#else
        const unsigned char sign8   = 0x80;                 // 1000 0000 B
        const unsigned char isign8b = 0x40;                 // 0100 0000 B
        const TYPE msign8        = VECTOR_SET1( sign8   );
        const TYPE misign8b      = VECTOR_SET1( isign8b );
#endif
        
#if PETIT == 1
#if MANUAL_PREFETCH == 1        
    _mm_prefetch((const char*)(p_indice_nod1[DEG_1]), _MM_HINT_T0); // prefetch En
    _mm_prefetch((const char*)(&p_msg1r[DEG_1]), _MM_HINT_T0);      // prefetch check msg
#endif
#endif
            #pragma unroll(DEG_1)
            for(int j=0 ; j<DEG_1 ; j++){   // subloop1
#if PETIT == 1
                TYPE vNoeud    = VECTOR_LOAD( *p_indice_nod1 );             // load En
#else
                TYPE vNoeud    = VECTOR_LOAD(&var_nodes[(*p_indice_nod1)]);
#endif
                TYPE vMessg    = VECTOR_LOAD(p_msg1r);                      // load check msg
                TYPE vContr    = VECTOR_SUB_AND_SATURATE_VAR_8bits(vNoeud, vMessg, min_var);    // get variable msg
                TYPE cSign     = VECTOR_GET_SIGN_BIT(vContr, msign8);       // get sign
                sign           = VECTOR_XOR(sign, cSign);                   // count sign
                TYPE vAbs      = VECTOR_MIN( VECTOR_ABS(vContr), max_msg ); // get abs msg for comparing
                tab_vContr[j]  = vContr;                                    // store variable msg
                TYPE vTemp     = min1;                                      // get min and submin
                min1           = VECTOR_MIN_1(vAbs, min1);
                min2           = VECTOR_MIN_2(vAbs, vTemp, min2);
                p_indice_nod1 += 1;                                         // next
                p_msg1r       += 1;
            }
            /* get min and submin with offset */
            TYPE cste_1   = VECTOR_MIN( VECTOR_SBU(min2, VECTOR_SET1(offset)), max_msg); // ON SATURE DIREECTEMENT AU FORMAT MSG
            TYPE cste_2   = VECTOR_MIN( VECTOR_SBU(min1, VECTOR_SET1(offset)), max_msg); // ON SATURE DIREECTEMENT AU FORMAT MSG

#if PETIT == 1
#if MANUAL_PREFETCH == 1        
    for(int j=0 ; j<DEG_1 ; j++){
        _mm_prefetch((const char*)(p_indice_nod1[j]), _MM_HINT_T0);
    }
    _mm_prefetch((const char*)(p_indice_nod1[DEG_1]), _MM_HINT_T0);
#endif
#endif
            
#if (DEG_1 & 0x01) == 1
            sign = VECTOR_XOR(sign, misign8);   // xor 1100 0000 B
#else
            sign = VECTOR_XOR(sign, misign8b);  // xor 0100 0000 B
#endif

            #pragma unroll(DEG_1)
            for(int j=0 ; j<DEG_1 ; j++) {      // subloop 2
                    TYPE vContr = tab_vContr[j];                                // get variable msg
                    TYPE vAbs   = VECTOR_MIN( VECTOR_ABS(vContr), max_msg );    // get abs check msg
                    TYPE vRes   = VECTOR_CMOV   (vAbs, min1, cste_1, cste_2);   // if vAbs == min1, set cste_1
                                                                                // else, set cste_2
                    TYPE vSig   = VECTOR_XOR    (sign, VECTOR_GET_SIGN_BIT(vContr, msign8));    // get sign
                    TYPE v2St   = VECTOR_invSIGN2(vRes, vSig);                                  // get new check msg
                    TYPE v2Sr   = VECTOR_ADD_AND_SATURATE_VAR_8bits(vContr, v2St, min_var);     // get new En
                    VECTOR_STORE( p_msg1w,                      v2St);          // store check msg
#if PETIT == 1
                    VECTOR_STORE( *p_indice_nod2, v2Sr);                        // store Eb
#else
                    VECTOR_STORE( &var_nodes[(*p_indice_nod2)], v2Sr);
#endif
                    p_msg1w        += 1;    // next
                    p_indice_nod2  += 1;
            }
#if PETIT == 1
#if MANUAL_PREFETCH == 1        
    _mm_prefetch((const char*)(*p_indice_nod2), _MM_HINT_T0);   // prefetch vn address
#endif
#endif
//IACA_END
//            arret = arret || VECTOR_XOR_REDUCE( sign );
        }

//////////////////////////
#if NB_DEGRES >= 2
        for (int i=0; i<DEG_2_COMPUTATIONS; i++){   // check msg update loop2

#if (DEG_2 & 0x01) == 1
        const unsigned char sign8   = 0x80;
        const unsigned char isign8  = 0xC0;
        const TYPE msign8        = VECTOR_SET1( sign8   );
        const TYPE misign8       = VECTOR_SET1( isign8  );
#else
        const unsigned char sign8   = 0x80;
        const unsigned char isign8b = 0x40;
        const TYPE msign8        = VECTOR_SET1( sign8   );
        const TYPE misign8b      = VECTOR_SET1( isign8b );
#endif
            
            TYPE tab_vContr[DEG_2];
            TYPE sign = zero;
            TYPE min1 = VECTOR_SET1(vSAT_POS_VAR);
            TYPE min2 = min1;

            #pragma unroll(DEG_2)
            for(int j=0 ; j<DEG_2 ; j++)
            {
#if PETIT == 1
                TYPE vNoeud    = VECTOR_LOAD( *p_indice_nod1 );
#else
                TYPE vNoeud    = VECTOR_LOAD(&var_nodes[(*p_indice_nod1)]);
#endif
                    TYPE vMessg = VECTOR_LOAD( p_msg1r );
                    TYPE vContr = VECTOR_SUB_AND_SATURATE_VAR_8bits(vNoeud, vMessg, min_var);
                    TYPE cSign  = VECTOR_GET_SIGN_BIT(vContr, msign8);
                    sign        = VECTOR_XOR (sign, cSign);
                    TYPE vAbs   = VECTOR_MIN( VECTOR_ABS(vContr), max_msg );
                    tab_vContr[j]  = vContr;
                    TYPE vTemp     = min1;
                    min1           = VECTOR_MIN_1(vAbs, min1      );
                    min2           = VECTOR_MIN_2(vAbs, vTemp, min2);
                    p_indice_nod1 += 1;
                    p_msg1r       += 1;
            }

            TYPE cste_1   = VECTOR_MIN( VECTOR_SBU(min2, VECTOR_SET1(offset)), max_msg); // ON SATURE DIREECTEMENT AU FORMAT MSG
            TYPE cste_2   = VECTOR_MIN( VECTOR_SBU(min1, VECTOR_SET1(offset)), max_msg); // ON SATURE DIREECTEMENT AU FORMAT MSG

#if (DEG_2 & 0x01) == 1
            sign = VECTOR_XOR(sign, misign8);
#else
            sign = VECTOR_XOR(sign, misign8b);
#endif

            #pragma unroll(DEG_2)
            for(int j=0 ; j<DEG_2 ; j++) {
                    TYPE vContr = tab_vContr[j];
                    TYPE vAbs   = VECTOR_MIN( VECTOR_ABS(vContr), max_msg );
                    TYPE vRes   = VECTOR_CMOV   (vAbs, min1, cste_1, cste_2);
                    TYPE vSig   = VECTOR_XOR    (sign, VECTOR_GET_SIGN_BIT(vContr, msign8));
                    TYPE v2St   = VECTOR_invSIGN2(vRes, vSig);
                    TYPE v2Sr   = VECTOR_ADD_AND_SATURATE_VAR_8bits(vContr, v2St, min_var);
                    VECTOR_STORE( p_msg1w,                      v2St);
#if PETIT == 1
                    VECTOR_STORE( *p_indice_nod2, v2Sr);
#else
                    VECTOR_STORE( &var_nodes[(*p_indice_nod2)], v2Sr);
#endif
                    p_msg1w        += 1;
                    p_indice_nod2  += 1;
            }
            
//            arret = arret || VECTOR_XOR_REDUCE( sign );
        }
#endif
#if NB_DEGRES > 2
    printf("The number of DEGREE(Cn) IS HIGHER THAN 2. YOU NEED TO PERFORM A COPY PASTE IN SOURCE CODE...\n");
    exit( 0 );
#endif
    }

    ////////////////////////////////////////////////////////////////////////////
    //
    // ON REMET EN FORME LES DONNEES DE SORTIE POUR LA SUITE DU PROCESS
    //
    if( NOEUD%32 == 0  ){
        uchar_itranspose_avx((TYPE*)var_nodes, (TYPE*)Rprime_fix, NOEUD);
    }else{
        char* ptr = (char*)var_nodes;
        for (int i=0; i<NOEUD; i+=1){
            for (int j=0; j<32; j+=1){
                Rprime_fix[j*NOEUD +i] = (ptr[32*i+j] > 0);     // decition
            }
        }
    }
    //
    ////////////////////////////////////////////////////////////////////////////
    return 1;
}
