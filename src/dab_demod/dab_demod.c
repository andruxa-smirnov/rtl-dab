/*
This file is part of rtl-dab
trl-dab is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Foobar is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with rtl-dab.  If not, see <http://www.gnu.org/licenses/>.


david may 2012
david.may.muc@googlemail.com

*/


#include "dab_demod.h"

int8_t dab_demod(dab_state *dab){
  uint32_t i,j;
  
  /* write input data into fifo */
  for (i=0;i<dab->input_buffer_len;i++) {
    cbWrite(&(dab->fifo),&dab->input_buffer[i]);
  }

  /* Check for data in fifo */
  if (dab->fifo.count < 196608*3) {
    return 0;
  }
  
  /* read fifo */
  dab_read_fifo(&(dab->fifo),196608*2,dab->coarse_timeshift+dab->fine_timeshift,dab->buffer);
  
  /* resetting coarse timeshift */
  dab->coarse_timeshift = 0;

  /* complex data conversion */
  for (j=0;j<196608*2;j+=2){
    dab->real[j/2]=dab->input_buffer[j]-127;
    dab->imag[j/2]=dab->input_buffer[j+1]-127;
  }
  
  /* coarse time sync */
  /* performance bottleneck atm */
  dab->coarse_timeshift = dab_coarse_time_sync(dab->real,dab->input_buffer,dab->filt);

  /* create complex frame */
  for (j=0;j<196608;j++){
    dab->dab_frame[j][0] = dab->real[j];
    dab->dab_frame[j][1] = dab->imag[j];
  }

  /* fine time sync */
  dab->fine_timeshift = dab_fine_time_sync(dab->dab_frame,dab->prs_ifft);

  /* coarse_frequency shift */
  fftw_plan p;
  p = fftw_plan_dft_1d(2048, &dab->dab_frame[2656+504+dab->fine_timeshift], dab->symbols[0], FFTW_FORWARD, FFTW_ESTIMATE);
  fftw_execute(p);
  fftw_complex tmp;
  for (i = 0; i < 2048/2; i++)
    {
      tmp[0]     = dab->symbols[0][i][0];
      tmp[1]     = dab->symbols[0][i][1];
      dab->symbols[0][i][0]    = dab->symbols[0][i+2048/2][0];
      dab->symbols[0][i][1]    = dab->symbols[0][i+2048/2][1];
      dab->symbols[0][i+2048/2][0] = tmp[0];
      dab->symbols[0][i+2048/2][1] = tmp[1];
    }
  dab->coarse_freq_shift = dab_coarse_freq_sync(dab->symbols[0]);


  /* fine freq correction */
  dab->fine_freq_shift = dab_fine_freq_corr(dab->dab_frame,dab->fine_timeshift);

  /* d-qpsk */
  for (i=1;i<76;i++) {
    p = fftw_plan_dft_1d(2048, &dab->dab_frame[2656+(2552*i)+504+dab->fine_timeshift],
		       dab->symbols[i], FFTW_FORWARD, FFTW_ESTIMATE);
  fftw_execute(p);
  for (j = 0; j < 2048/2; j++)
    {
      tmp[0]     = dab->symbols[i][j][0];
      tmp[1]     = dab->symbols[i][j][1];
      dab->symbols[i][j][0]    = dab->symbols[i][j+2048/2][0];
      dab->symbols[i][j][1]    = dab->symbols[i][j+2048/2][1];
      dab->symbols[i][j+2048/2][0] = tmp[0];
      dab->symbols[i][j+2048/2][1] = tmp[1];
    }
  
  }
  for (j=1;j<75;j++) {
    for (i=0;i<2048;i++)
      {
	dab->symbols_d[(j-1)*2048+i][0] = ((dab->symbols[j][i][0]*dab->symbols[j-1][i][0])
					   +(dab->symbols[j][i][1]*dab->symbols[j-1][i][1]))
	  /(dab->symbols[j-1][i][0]*dab->symbols[j-1][i][0]+dab->symbols[j-1][i][1]*dab->symbols[j-1][i][1]);
	dab->symbols_d[(j-1)*2048+i][1] = ((dab->symbols[j][i][0]*dab->symbols[j-1][i][1])
					   -(dab->symbols[j][i][1]*dab->symbols[j-1][i][0]))
	  /(dab->symbols[j-1][i][0]*dab->symbols[j-1][i][0]+dab->symbols[j-1][i][1]*dab->symbols[j-1][i][1]);
      }
  }
  int32_t k=0;
  for (j=0;j<75;j++) {
    for (i=0;i<2048;i++){
      if ((i>255) && i!=1024 && i < 1793) {
	dab->symbols_dc[j*1536+k][0] = dab->symbols_d[j*2048+i][0];
	dab->symbols_dc[j*1536+k][1] = dab->symbols_d[j*2048+i][1];
	k++;
	}
    }
    k = 0;
  }

  /* frequency deinterleaving */
  for (i=0;i<75;i++){
    for (j=0;j<1536;j++) {
      
      k=dab->f_interl_table[j];
      if (k<0) {
	k = (1536/2) + k;
      }
      else if (k>0) {
	k = (1536/2) + k - 1;
      }
      dab->symbols_dc_fd[(i*1536)+j][0] = dab->symbols_dc[(i*1536)+k][0];
      dab->symbols_dc_fd[(i*1536)+j][1] = dab->symbols_dc[(i*1536)+k][1];
    } 
  }
  
  /* demapping */
  for (i=0;i<75;i++){
    for (j=0;j<1536*2;j++){
      if (j<1536){
	dab->symbols_demapped[i][j] = (dab->symbols_dc_fd[(i*1536)+j][0]>0) ? 0:1;
	}
      else if (j>=1536) {
	dab->symbols_demapped[i][j] = (dab->symbols_dc_fd[(i*1536)+(j-1536)][1] > 0) ? 1:0;
      }
    }
  }

  /* block partitioning */
  
  for (i=0;i<1536*2;i++){
          dab->FIC[i+3072*0] = dab->symbols_demapped[0][i];
          dab->FIC[i+3072*1] = dab->symbols_demapped[1][i];
          dab->FIC[i+3072*2] = dab->symbols_demapped[2][i];
  }
  
  /* FIC depuncture */
  dab_fic_depuncture(&dab->FIC[0],&dab->FIC_dep[0]);
  dab_fic_depuncture(&dab->FIC[2304*1],&dab->FIC_dep[3096*1]);
  dab_fic_depuncture(&dab->FIC[2304*2],&dab->FIC_dep[3096*2]);
  dab_fic_depuncture(&dab->FIC[2304*3],&dab->FIC_dep[3096*3]);

  /* Vitdec */

  viterbi( &dab->FIC_dep[3096*0], 3096, &dab->FIC_dep_dec[768*0]);
  viterbi( &dab->FIC_dep[3096*1], 3096, &dab->FIC_dep_dec[768*1]);
  viterbi( &dab->FIC_dep[3096*2], 3096, &dab->FIC_dep_dec[768*2]);
  viterbi( &dab->FIC_dep[3096*3], 3096, &dab->FIC_dep_dec[768*3]);


  /*De-scramble */
  dab_fic_descramble( &dab->FIC_dep_dec[768*0],  &dab->FIC_dep_dec_scr[768*0], 768);
  dab_fic_descramble( &dab->FIC_dep_dec[768*1],  &dab->FIC_dep_dec_scr[768*1], 768);
  dab_fic_descramble( &dab->FIC_dep_dec[768*2],  &dab->FIC_dep_dec_scr[768*2], 768);
  dab_fic_descramble( &dab->FIC_dep_dec[768*3],  &dab->FIC_dep_dec_scr[768*3], 768);
  
  

return 1;

}


void dab_demod_init(dab_state * dab){
  cbInit(&(dab->fifo),(196608*2*4)); // 4 frames
  dab->coarse_timeshift = 0;
  dab->fine_timeshift=0;
  dab->dab_frame = ( fftw_complex* ) fftw_malloc( sizeof( fftw_complex ) * 196608 );
  dab->prs_ifft =( fftw_complex*) fftw_malloc(sizeof(fftw_complex) * (2048 + 32));
  dab->prs_conj_ifft = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * (2048 + 32));
  dab->prs_syms = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * (2048 + 32));
  prsgen(dab->prs_ifft,dab->prs_conj_ifft,dab->prs_syms);
  dab->symbols_d = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * 2048 * 75);
  dab->symbols_dc = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * 1536 * 75);
  dab->symbols_dc_fd = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * 1536 * 75);

}
