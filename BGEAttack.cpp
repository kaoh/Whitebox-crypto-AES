/*
 * BGEAttack.cpp
 *
 *  Created on: Apr 7, 2013
 *      Author: ph4r05
 */

#include "BGEAttack.h"

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <ctime>

namespace wbacr {
namespace attack {

BGEAttack::BGEAttack() {
	;

}

BGEAttack::~BGEAttack() {
	;
}

using namespace std;
using namespace NTL;

int BGEAttack::shiftIdentity[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};

std::string composeFunction(GF256_func_t f, GF256_func_t g){
	unsigned int x;	// warning, if is type BYTE, then it will overflow and loop forever
	fction_t nf;
	for(x=0; x<=(GF256-1); x++){
		//cout << "; X=" << x << endl;
		//cout << "; g["<<CHEX(x)<<"] = " CHEX(g[x]) << ";" << endl;
		//cout << "; f[g["<<CHEX(x)<<"]] = " CHEX(f[g[x]]) << ";" << endl;

		nf.f[x] = f[g[x]];
		nf.finv[f[g[x]]] = x;
	}

	return hashFunction(nf.f);
}

void BGEAttack::Rbox(W128b& state, bool encrypt, int r, bool noShift){
	int i=0;
	W32b ires[N_BYTES];				// intermediate result for T-boxes

	// encryption/decryption dependent operations and tables
	int (&shiftOp)[N_BYTES]  = noShift ? this->shiftIdentity : (encrypt ? (this->wbaes.shiftRows)   : (this->wbaes.shiftRowsInv));
	W32XTB (&edXTab)[N_ROUNDS][N_SECTIONS][N_XOR_GROUPS] = encrypt ? (this->wbaes.eXTab) 	   : (this->wbaes.dXTab);
	AES_TB_TYPE2 (&edTab2)[N_ROUNDS][N_BYTES]			 = encrypt ? (this->wbaes.eTab2) 	   : (this->wbaes.dTab2);
	AES_TB_TYPE3 (&edTab3)[N_ROUNDS][N_BYTES]			 = encrypt ? (this->wbaes.eTab3) 	   : (this->wbaes.dTab3);

	// Perform rest of the operations on 4 tuples.
	for(i=0; i<N_BYTES; i+=4){
		// Apply type 2 tables to all bytes, counting also shift rows selector.
		// One section ~ 1 column of state array, so select 1 column, first will
		// have indexes 0,4,8,12. Also take ShiftRows() into consideration.
		ires[i+0].l = edTab2[r][i+0][state.B[shiftOp[i/4+0*4]]].l;
		ires[i+1].l = edTab2[r][i+1][state.B[shiftOp[i/4+1*4]]].l;
		ires[i+2].l = edTab2[r][i+2][state.B[shiftOp[i/4+2*4]]].l;
		ires[i+3].l = edTab2[r][i+3][state.B[shiftOp[i/4+3*4]]].l;

		// In the last round, result is directly in T2 boxes
		if (r==(N_ROUNDS-1)){
			continue;
		}

		// XOR results of T2 boxes
		op8xor(ires[i+0], ires[i+1], edXTab[r][i/4][0], ires[i+0]);  // 1 xor 2
		op8xor(ires[i+2], ires[i+3], edXTab[r][i/4][1], ires[i+2]);  // 3 xor 4
		op8xor(ires[i+0], ires[i+2], edXTab[r][i/4][2], ires[i+0]);  // (1 xor 2) xor (3 xor 4) - next XOR stage

		// Apply T3 boxes, valid XOR results are in ires[0], ires[4], ires[8], ires[12]
		// Start from the end, because in ires[i] is our XORing result.
		//
		//                    ________________________ ROUND
		//                   |    ____________________ T3 box for 1 section
		//                   |   |      ______________ (1 xor 2) xor (3 xor 4)
		//                   |   |     |        ______ 8bit parts of 32 bit result
		//                   |   |     |       |
		ires[i+3].l = edTab3[r][i+3][ires[i].B[3]].l;
		ires[i+2].l = edTab3[r][i+2][ires[i].B[2]].l;
		ires[i+1].l = edTab3[r][i+1][ires[i].B[1]].l;
		ires[i+0].l = edTab3[r][i+0][ires[i].B[0]].l;

		// Apply XORs again, now on T3 results
		// Copy results back to state
		op8xor(ires[i+0], ires[i+1], edXTab[r][i/4][3], ires[i+0]);  // 1 xor 2
		op8xor(ires[i+2], ires[i+3], edXTab[r][i/4][4], ires[i+2]);  // 3 xor 4
		op8xor(ires[i+0], ires[i+2], edXTab[r][i/4][5], ires[i+0]);  // (1 xor 2) xor (3 xor 4) - next XOR stage
	}

	//
	// Copy results back to state
	// ires[i] now contains 32bit XOR result
	// We have to copy result to column...
	for(i=0; i<N_BYTES; i+=4){
		state.B[i/4+ 0] = r<(N_ROUNDS-1) ? ires[i].B[0] : ires[i+0].B[0];
		state.B[i/4+ 4] = r<(N_ROUNDS-1) ? ires[i].B[1] : ires[i+1].B[0];
		state.B[i/4+ 8] = r<(N_ROUNDS-1) ? ires[i].B[2] : ires[i+2].B[0];
		state.B[i/4+12] = r<(N_ROUNDS-1) ? ires[i].B[3] : ires[i+3].B[0];
	}
}

void BGEAttack::recoverPsi(Sset_t & S){
	Rmap_t R;
	int e = 1;

	// copy function map - writable copy (removable); fction map is hash -> idx
	fctionMap_t Stmp = S.fmap;

	// R = {'id', ['00']}; psi[id] = 0
	// R is fIdx -> beta
	R.insert(Rmap_elem_t(0,0));
	S.psi[0] = 0;

	// remove identity function from set
	Stmp.erase(S.fctions[0].hash);

	// start finding vector base & psi
	while(R.size() < GF256 && Stmp.size() > 0){
		// S <- S \ {f} // pick f from S and remove from S
		fctionMap_t::iterator it1 = Stmp.begin();
		BYTE fIdx = it1->second;
		Stmp.erase(it1);

		// if is already generated by some base, skip it
		if (R.count(fIdx)>0) continue;

		S.psi[fIdx] = e;
		//cout << "Taking fIdx="<<CHEX(fIdx)<<" as new base element e_i="<<CHEX(e)<<endl;

		Rmap_t Rcopy = R;
		for(Rmap_t::const_iterator it=Rcopy.begin(); it != Rcopy.end(); ++it){
			// Now compute elements (f \ocirc g, [e] ^ [n])
			// Functions should form vector space thus composition two of them should
			// result in another function from vector space. Thus compute hash of composition.

			// DEBUG
			//cout << " fIdx hash = " << S.fctions[fIdx].hash << endl;
			//cout << " firsthash = " << S.fctions[it->first].hash << endl;

			std::string nhash = composeFunction(S.fctions[fIdx].f, S.fctions[it->first].f);

			// hash should be contained in Stmp
			if (S.fmap.count(nhash)==0){
				cerr << "nhash["<<nhash<<"] was not found in VectorSpace; composition f \\ocirc g = " << CHEX(fIdx) << " o " << CHEX(it->first)
					 << "; betarepr: e="<<CHEX(e)<<"; ni="<<CHEX(it->second)<< endl;
				assert(S.fmap.count(nhash)>0);
			}

			BYTE nIdx = S.fmap[nhash];
			R.insert(Rmap_elem_t(nIdx, e ^ it->second));
			S.psi[nIdx] = e ^ it->second;
		}

		e *= 0x2;			// base move
	}

	if (R.size() < GF256 && Stmp.size() == 0){
		cerr << "Something bad happened, S is empty, R does not span whole vector space. size=" << R.size() << endl;
	}
}

void BGEAttack::run(void) {
	cout << "Started with attack" << endl;

	GenericAES defAES;
	defAES.init(0x11B, 0x03);

	WBAESGenerator generator;
	CODING8X8_TABLE coding[16];
	W128b state;
	cout << "Generating AES..." << endl;
	generator.generateIO128Coding(coding, true);
	generator.generateTables(GenericAES::testVect128_key, KEY_SIZE_16, this->wbaes, coding, true);  cout << "AES ENC generated" << endl;
	generator.generateTables(GenericAES::testVect128_key, KEY_SIZE_16, this->wbaes, coding, false); cout << "AES DEC generated" << endl;


	//
	//
	// Attack below...
	//
	//

	// Recover affine parts of Q for round r=0
	int r = 0;

	// At first compute base function f_{00}, we will need it for computing all next functions,
	// to be exact, its inverse, f^{-1}_{00}
	cout << "Allocating memory for attack" << endl;
	SsetPerRound_t * Sr = new SsetPerRound_t[10];
	Qaffine_t * Qaffine = new Qaffine_t;
	cout << "Memory allocated; Sr=" << dec << (sizeof(SsetPerRound_t)*10) << "; Qaffine=" << dec << sizeof(Qaffine_t) << endl;
	cout << "Memory allocated totally: " << (((sizeof(SsetPerRound_t)*10) + sizeof(Qaffine_t)) / 1024.0 / 1024.0) << " MB" << endl;

	for(r=0; r<9; r++){
		int row=0;
		int col=0;
		int x = 0;
		int i = 0;
		int j = 0;
		int c1;

		// Init f_00 function in Sr
		for(i=0; i<AES_BYTES; i++){
			Sr[r].S[i%4][i/4].f_00.c1 = 0;
		}

		//
		// Compute f(x,0,0,0) function for each Q_{i,j}
		//
		// x x x x        y_{0,0} y_{1,0} ..
		// 0 0 0 0   R    y_{0,1} y_{1,1} ..
		// 0 0 0 0  --->  y_{0,2} y_{1,2} ..
		// 0 0 0 0        y_{0,3} y_{1,3} ..
		//
		cout << "Generating f_00 for round r="<<r<<endl;
		for(x=0; x<=0xff; x++){
			memset(&state, 0, sizeof(state));		// put 0 everywhere
			state.B[0]=x;   state.B[1]=x; 			// init with x values for y_0 in each column
			state.B[2]=x;   state.B[3]=x;           // recall that state array is indexed by rows.

			this->Rbox(state, true, r, true);		// perform R box computation on input & output values
			for(i=0; i<AES_BYTES; i++){
				fction_t & f00 = Sr[r].S[i%4][i/4].f_00;
				f00.f[x] = state.B[i];
				f00.finv[state.B[i]] = x;
			}
		}

		// f(x,0,0,0) finalization - compute hash
		for(i=0; i<AES_BYTES; i++){
			Sr[r].S[i%4][i/4].f_00.initHash();
		}

		// now generate f(x,0,0,0) .. f(x,0xff,0,0) functions, generate then corresponding sets and whole Sr for round r
		cout << "Generating set S..." << endl;
		for(c1=0; c1<=0xff; c1++){
			// init f_c1 functions
			for(i=0; i<AES_BYTES; i++){
				Sr[r].S[i%4][i/4].fctions[c1].c1 = c1;
			}

			// Now generating function f(x,c1,0,0)
			//
			//  x  x  x  x        y_{0,0} y_{1,0} ..
			// c1 c1 c1 c1   R    y_{0,1} y_{1,1} ..
			//  0  0  0  0  --->  y_{0,2} y_{1,2} ..
			//  0  0  0  0        y_{0,3} y_{1,3} ..
			//
			//
			// Generating S[col][row] fctions as f(f^{-1}(x,0,0,0), c1, 0, 0)
			// To be exact it is f( f^{-1}(x,0,0,0)[y0], c1, 0, 0)[y0]   for y0
			//
			// So y0 must match in both functions (inverse f and f), thus we have to iterate y_i, i \in [0,3]
			// One calculation for y_0 (4 columns simultaneously), another iteration for y_1 etc...
			// This loop iterates over ROWS of functions f.
			for(row=0; row<4; row++){
				for(x=0; x<=0xff; x++){
					// Init input arguments to (x,c1,0,0) for each column
					memset(&state, 0, sizeof(state));

					state.B[0]=Sr[r].S[0][row].f_00.finv[x]; // f^{-1}(x,0,0,0)[y0], 1.st column
					state.B[1]=Sr[r].S[1][row].f_00.finv[x]; // f^{-1}(x,0,0,0)[y1], 2.nd column
					state.B[2]=Sr[r].S[2][row].f_00.finv[x]; // f^{-1}(x,0,0,0)[y2], 3.rd column
					state.B[3]=Sr[r].S[3][row].f_00.finv[x]; // f^{-1}(x,0,0,0)[y3], 4.th column

					// init second argument as c1
					state.B[4]=c1;  state.B[5]=c1;
					state.B[6]=c1;  state.B[7]=c1;

					this->Rbox(state, true, r, true);		// perform R box computation on input & output values
					for(i=0; i<4; i++){  					// if we are in row=3, take result from 3rd row of state array: 8,9,10,11. Iterating over columns
						Sr[r].S[i][row].fctions[c1].f[x] = state.B[4*row + i];
						Sr[r].S[i][row].fctions[c1].finv[state.B[4*row + i]] = x;
					}
				}
			}
		}

		// Insert hash of functions to corresponding S set
		for(i=0; i<AES_BYTES; i++){
			Sr[r].S[i%4][i/4].fmap.clear();
			for(j=0; j<GF256; j++){
				Sr[r].S[i%4][i/4].fctions[j].initHash();
				std::string & hash = Sr[r].S[i%4][i/4].fctions[j].hash;
				if (Sr[r].S[i%4][i/4].fmap.count(hash)!=0){
					cerr << "Sr["<<r<<"].["<<(i%4)<<"]["<<(i/4)<<"] set already contains hash[" << hash << "]; for j=" << j << std::endl;
				}

				Sr[r].S[i%4][i/4].fmap.insert(fctionMap_elem_t(hash, j));
			}
		}



		// Now we have Sr[r] generated.
		// According to the paper, we now have to construct isomorphism
		// \phi S --> GF(2)^8
		//      Q \op \oplus_{\beta} \op Q^{-1}  --> [\beta]
		//
		// But we don't know this isomorphism. Instead we will construct isomorphism \psi
		// \psi = L^{-1} \op \phi, where L is base change matrix (linear transformation) [e_i] --> [\Beta_i]
		//
		// We can find base set in S and express every element in S by means of base. This way we will find \psi.
		//
		for(i=0; i<AES_BYTES; i++){
			cout << "Recovering psi for Sidx="<<i<<endl;
			Sset_t & curS = Sr[r].S[i%4][i/4];
			recoverPsi(curS);

			// derive Q~
			//                   +-------------------------------- Q^{-1} \ocirc L^{-1}; 1,2 part of commutative graph
			//                   |                          +----- 3.rd part of commutative graph (on purpose, to be able to construct PSI)
			//                   |                          |
			// 1. Q~('00') = L^{-1}(Q^{-1}('00')) \oplus (Q \ocirc L)^{-1}('00') = ['00']
			// 2. f = Q~ \ocirc \oplus_{PSI(f)} \ocirc Q~{-1}
			//
			// (1., 2.) ==>f('00') = Q~(PSI(f)) ==>
			// Q~: x is PSI(f) for some f (we have 256 of them)
			// Q~: y is f('00')  --- || ---
			for(j=0; j<GF256; j++){
				    x = curS.psi[j];
				int y = curS.fctions[j].f[0];

				Qaffine->Q[r][i].f[x]    = y;
				Qaffine->Q[r][i].finv[y] = x;
			}

			Qaffine->Q[r][i].initHash();
			cout << "Q~ recovered; hash=" << Qaffine->Q[r][i].hash << endl;

			// Now we can reduce P, Q are non-linear & matching state to P,Q are affine and matching
			// P^{r+1}_{i,j}  = Q^r_{i,j} (they are matching)
			// P~^{r+1}_{i,j} = Q~^r_{i,j} (they are matching, also holds for new affine mappings)
			//
			// Reduction:
			// Q~^{-1}(Q(x)) = L^{-1}(x) \oplus L^{-1}(Q^{-1}('00'))  .... what is affine (last term is constant)
			//
			// Thus it is enough to apply Q~^{-1} to the end of R-box,
			// To preserve matching relation, we also apply Q~ before T2 boxes,
			// Before it was: MixCol -> Q           -> |new round|       -> Q^{-1} -> T2box
			// Now it will be MixCol -> Q -> Q~{-1} -> |new round| Q~    -> Q^{-1} -> T2box
			// It is easy to see that it matches...
			//
			// Thus P^{r+1} conversion to affine matching: P^{r+1}(Q~(x)) =
			// = Q^{-1}(Q~(x)) = Q^{-1}( Q(L( x   \oplus L^{-1}(Q^{-1}('00')))))
			//                 =           L( x   \oplus L^{-1}(Q^{-1}('00')))
			//                 =           L( x ) \oplus        Q^{-1}('00')      ==> affine

			// TODO: apply above description and reduce to affine matching transformations
			// Q will be on this->wbaes.eXTab[r][i%4][5][0..7][0..256]
			// P will be on this->wbaes.dTab2[r][shiftInvRows(i) -- as with L^{-1} matching][0..256]
		}

		cout << "PSI recovered for all sets in given round" << endl;
	}

	delete[] Sr;
	delete Qaffine;

}

} /* namespace attack */
} /* namespace wbacr */
