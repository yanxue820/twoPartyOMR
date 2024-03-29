/*
Authors: Deevashwer Rathee, Mayank Rathee
Copyright:
Copyright (c) 2020 Microsoft Research
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef BATCHEQUALITYSPLIT_H__
#define BATCHEQUALITYSPLIT_H__
#include "OT/emp-ot.h"
#include "utils/emp-tool.h"
#include "Millionaire/bit-triple-generator.h"
#include <cmath>
#include<ctime>
#include <thread>

using namespace sci;
using namespace std;

template<typename IO>
class BatchEqualitySplit {
	public:
		IO* io1 = nullptr;
    IO* io2 = nullptr;
		sci::OTPack<IO>* otpack1, *otpack2;
		TripleGenerator<IO>* triple_gen1, *triple_gen2;
		int party;
		int l, r, log_alpha, beta, beta_pow, batch_size, radixArrSize;
		int num_digits, num_triples_corr, num_triples_std, log_num_digits, num_cmps;
		int num_triples;
		uint8_t mask_beta, mask_r;
		Triple* triples_std;
    uint8_t* leaf_eq;
		int total_triples_count, triples_count, triples_count_1;

		BatchEqualitySplit(int party,
				int bitlength,
				int log_radix_base,
				int batch_size,
				int num_cmps)
		{
			assert(log_radix_base <= 8);
			assert(bitlength <= 64);
			this->party = party;
			this->l = bitlength;
			this->beta = log_radix_base;
			this->batch_size = batch_size;
			this->num_cmps = num_cmps;
			configure();
		}

		void configure()
		{
			this->num_digits = ceil((double)l/beta);
			this->r = l % beta;
			this->log_alpha = sci::bitlen(num_digits) - 1;
			this->log_num_digits = log_alpha + 1;
			this->num_triples = num_digits-1;
			if (beta == 8) this->mask_beta = -1;
			else this->mask_beta = (1 << beta) - 1;
			this->mask_r = (1 << r) - 1;
			this->beta_pow = 1 << beta;
			total_triples_count = (num_triples)*batch_size*num_cmps;
      //total_triples
			this->triples_std = new Triple((num_triples)*batch_size*num_cmps, true);
			//this->triples_std_1 = new Triple((num_triples)*batch_size*num_cmps, true);
		}

		~BatchEqualitySplit()
		{
			delete triple_gen1;
      delete triple_gen2;
		}

		void computeLeafOTs(uint64_t* data)
		{

			struct timespec start, finish, lomstart, lomfinish, locstart, locfinish;

			clock_gettime(CLOCK_MONOTONIC, &start);
			uint8_t* digits; // num_digits * num_cmps

			if(this->party == sci::ALICE) {
    		radixArrSize = batch_size*num_cmps;
  		} else {
				radixArrSize = num_cmps;
  		}

			digits = new uint8_t[num_digits*radixArrSize];
			leaf_eq = new uint8_t[num_digits*batch_size*num_cmps];

			// Extract radix-digits from data
			for(int i = 0; i < num_digits; i++) // Stored from LSB to MSB
				for(int j = 0; j < radixArrSize; j++)
					if ((i == num_digits-1) && (r != 0))
						digits[i*radixArrSize+j] = (uint8_t)(data[j] >> i*beta) & mask_r;
					else
						digits[i*radixArrSize+j] = (uint8_t)(data[j] >> i*beta) & mask_beta;

			if(party == sci::ALICE)
			{
	    	uint8_t** leaf_ot_messages; // (num_digits * num_cmps) X beta_pow (=2^beta)
				leaf_ot_messages = new uint8_t*[num_digits*num_cmps];
				for(int i = 0; i < num_digits*num_cmps; i++)
					leaf_ot_messages[i] = new uint8_t[beta_pow];

        clock_gettime(CLOCK_MONOTONIC, &lomstart);
				// Set Leaf OT messages
				triple_gen1->prg->random_bool((bool*)leaf_eq, batch_size*num_digits*num_cmps);

				for(int i = 0; i < num_digits; i++) {
					for(int j = 0; j < num_cmps; j++) {
						if (i == (num_digits - 1) && (r > 0)){
#ifdef WAN_EXEC
							set_leaf_ot_messages(leaf_ot_messages[i*num_cmps+j], digits,
									beta_pow, leaf_eq, i, j);
#else
						  set_leaf_ot_messages(leaf_ot_messages[i*num_cmps+j], digits,
									1 << r, leaf_eq, i, j);
#endif
						}
						else{
							set_leaf_ot_messages(leaf_ot_messages[i*num_cmps+j], digits,
									beta_pow, leaf_eq, i, j);
						}
					}
				}
				clock_gettime(CLOCK_MONOTONIC, &lomfinish);

				clock_gettime(CLOCK_MONOTONIC, &locstart);

				// Perform Leaf OTs
#ifdef WAN_EXEC
				otpack1->kkot_beta->send(leaf_ot_messages, num_cmps*(num_digits), 3);
#else
				if (r == 1) {
					otpack1->kkot_beta->send(leaf_ot_messages, num_cmps*(num_digits-1), 3);
					otpack1->iknp_straight->send(leaf_ot_messages+num_cmps*(num_digits-1), num_cmps, 3);
				}
				else if (r != 0) {
					otpack1->kkot_beta->send(leaf_ot_messages, num_cmps*(num_digits-1), 3);
					if(r == 2){
						otpack1->kkot_4->send(leaf_ot_messages+num_cmps*(num_digits-1), num_cmps, 3);
					}
					else if(r == 3){
						otpack1->kkot_8->send(leaf_ot_messages+num_cmps*(num_digits-1), num_cmps, 3);
					}
					else if(r == 4){
						otpack1->kkot_16->send(leaf_ot_messages+num_cmps*(num_digits-1), num_cmps, 3);
					}
					else{
						throw std::invalid_argument("Not yet implemented!");
					}
				}
				else {
					otpack1->kkot_beta->send(leaf_ot_messages, num_cmps*num_digits, 3);
				}
#endif
				// Cleanup
				for(int i = 0; i < num_digits*num_cmps; i++)
					delete[] leaf_ot_messages[i];
				delete[] leaf_ot_messages;
				clock_gettime(CLOCK_MONOTONIC, &locfinish);
				double total_time = (lomfinish.tv_sec - lomstart.tv_sec);
				total_time += (lomfinish.tv_nsec - lomstart.tv_nsec) / 1000000000.0;
				std::cout<<"Leaf OT Message Time: "<<total_time<<std::endl;
				total_time = (locfinish.tv_sec - locstart.tv_sec);
				total_time += (locfinish.tv_nsec - locstart.tv_nsec) / 1000000000.0;
				std::cout<<"Leaf OT Comm. Time "<<total_time<<std::endl;

			}
			else // party = sci::BOB
			{ //triple_gen1->generate(3-party, triples_std, _16KKOT_to_4OT);
				// Perform Leaf OTs
#ifdef WAN_EXEC
				otpack1->kkot_beta->recv(leaf_eq, digits, num_cmps*(num_digits), 3);
#else
				if (r == 1) {
					otpack1->kkot_beta->recv(leaf_eq, digits, num_cmps*(num_digits-1), 3);
					otpack1->iknp_straight->recv(leaf_eq+num_cmps*(num_digits-1),
							digits+num_cmps*(num_digits-1), num_cmps, 3);
				}
				else if (r != 0) {
					otpack1->kkot_beta->recv(leaf_eq, digits, num_cmps*(num_digits-1), 3);
					if(r == 2){
						otpack1->kkot_4->recv(leaf_eq+num_cmps*(num_digits-1),
								digits+num_cmps*(num_digits-1), num_cmps, 3);
					}
					else if(r == 3){
						otpack1->kkot_8->recv(leaf_eq+num_cmps*(num_digits-1),
								digits+num_cmps*(num_digits-1), num_cmps, 3);
					}
					else if(r == 4){
						otpack1->kkot_16->recv(leaf_eq+num_cmps*(num_digits-1),
								digits+num_cmps*(num_digits-1), num_cmps, 3);
					}
					else{
						throw std::invalid_argument("Not yet implemented!");
					}
				}
				else {
					otpack1->kkot_beta->recv(leaf_eq, digits, num_cmps*(num_digits), 3);
				}
#endif

				// Extract equality result from leaf_res_cmp
				for(int i = 0; i < num_digits*num_cmps; i++) {
		      for(int j=batch_size-1; j>= 0; j--) {
						leaf_eq[j*num_digits*num_cmps+ i] = (leaf_eq[i]>>j) & 1;
					}
				}
			}

			clock_gettime(CLOCK_MONOTONIC, &finish);
			double total_time = (finish.tv_sec - start.tv_sec);
  		total_time += (finish.tv_nsec - start.tv_nsec) / 1000000000.0;
      std::cout<<"Leaf OT-Time: "<<total_time<<std::endl;

			/*for(int i=0; i<10; i++) {
				for(int j=0;j<batch_size; j++) {
					std::cout<< (int)leaf_eq[j*num_digits*num_cmps+ i] << " ";
				}
				std::cout<< std::endl;
			}*/
			/*for (int i = 0; i < num_cmps; i++)
				res[i] = leaf_res_cmp[i];
     */
			// Cleanup
			delete[] digits;
		}

		void set_leaf_ot_messages(uint8_t* ot_messages,
				uint8_t* digits,
				int N,
				uint8_t* mask_bytes,
				int i,
				int j)
		{
			for(int k = 0; k < N; k++) {
				ot_messages[k] = 0;
				for(int m=0; m < batch_size; m++) {
					ot_messages[k] = ot_messages[k] | (((digits[i*radixArrSize + j*batch_size + m] == k) ^ mask_bytes[m*num_digits*num_cmps + i*num_cmps+j]) << m);
				}
			}
		}

		/**************************************************************************************************
		 *                         AND computation related functions
		 **************************************************************************************************/

    void generate_triples() {
			struct timespec start, finish;
			clock_gettime(CLOCK_MONOTONIC, &start);
      triple_gen2->generate(3-party, triples_std, _16KKOT_to_4OT);
			clock_gettime(CLOCK_MONOTONIC, &finish);
			double total_time = (finish.tv_sec - start.tv_sec);
  		total_time += (finish.tv_nsec - start.tv_nsec) / 1000000000.0;
      std::cout<<"Triple Generation Time: "<<total_time<<std::endl;
    }

		void traverse_and_compute_ANDs(){

			struct timespec start, finish, lomstart, lomfinish, locstart, locfinish;

			//if(sci::ALICE) {

			//}

			//std::cout << "Num Triples are: " << num_triples<< std::endl;

			std::cout<<"CP 1"<< std::endl;
			clock_gettime(CLOCK_MONOTONIC, &start);

			std::cout<<"CP 2"<< std::endl;
			//clock_gettime(CLOCK_MONOTONIC, &start);
			// Combine leaf OT results in a bottom-up fashion
			int counter_std = 0, old_counter_std = 0;
			int counter_corr = 0, old_counter_corr = 0;
			int counter_combined = 0, old_counter_combined = 0;
			uint8_t* ei = new uint8_t[(num_triples*batch_size*num_cmps)/8];
			uint8_t* fi = new uint8_t[(num_triples*batch_size*num_cmps)/8];
			uint8_t* e = new uint8_t[(num_triples*batch_size*num_cmps)/8];
			uint8_t* f = new uint8_t[(num_triples*batch_size*num_cmps)/8];

			int old_triple_count=0, triple_count=0;

			for(int i = 1; i < num_digits; i*=2) {
				//std::cout<<"Level " << i << std::endl;
				int counter=0;
				for(int j = 0; j < num_digits and j+i < num_digits; j += 2*i) {
					//std::cout<<"Pair (" << j << "," << j+1<< ")"<< std::endl;
					for(int k=0; k < batch_size; k++) {
						//std::cout<<"Batch "<< k << std::endl;
						for(int m=0; m < num_cmps; m+=8) {
							//std::cout<<"Comparison Number "<< m << std::endl;
							ei[(counter*batch_size*num_cmps + k*num_cmps + m)/8] = triples_std->ai[(triple_count+ counter*batch_size*num_cmps + k*num_cmps + m)/8];
							fi[(counter*batch_size*num_cmps + k*num_cmps + m)/8] = triples_std->bi[(triple_count+ counter*batch_size*num_cmps + k*num_cmps + m)/8];
							ei[(counter*batch_size*num_cmps + k*num_cmps + m)/8] ^= sci::bool_to_uint8(leaf_eq + j*num_cmps + k*num_digits*num_cmps + m, 8);
							fi[(counter*batch_size*num_cmps + k*num_cmps + m)/8] ^= sci::bool_to_uint8(leaf_eq + (j+i)*num_cmps + k*num_digits*num_cmps + m, 8);
						}
					}
					counter++;
				}
				triple_count += counter*batch_size*num_cmps;
				int comm_size = (counter*batch_size*num_cmps)/8;

				if(party == sci::ALICE)
				{
					io1->send_data(ei, comm_size);
					io1->send_data(fi, comm_size);
					io1->recv_data(e, comm_size);
					io1->recv_data(f, comm_size);
				}
				else // party = sci::BOB
				{
					io1->recv_data(e, comm_size);
					io1->recv_data(f, comm_size);
					io1->send_data(ei, comm_size);
					io1->send_data(fi, comm_size);
				}

				for(int i = 0; i < comm_size; i++) {
					e[i] ^= ei[i];
					f[i] ^= fi[i];
				}

				counter=0;
				for(int j = 0; j < num_digits and j+i < num_digits; j += 2*i) {
					for(int k=0; k < batch_size; k++) {
						for(int m=0; m < num_cmps; m+=8) {
							uint8_t temp_z;
							if (party == sci::ALICE)
								temp_z = e[(counter*batch_size*num_cmps + k*num_cmps + m)/8] & f[(counter*batch_size*num_cmps + k*num_cmps + m)/8];
							else
								temp_z = 0;

							temp_z ^= f[(counter*batch_size*num_cmps + k*num_cmps + m)/8] & triples_std->ai[(old_triple_count+ counter*batch_size*num_cmps + k*num_cmps + m)/8];
							temp_z ^= e[(counter*batch_size*num_cmps + k*num_cmps + m)/8] & triples_std->bi[(old_triple_count+ counter*batch_size*num_cmps + k*num_cmps + m)/8];
							temp_z ^= triples_std->ci[(old_triple_count+ counter*batch_size*num_cmps + k*num_cmps + m)/8];
							sci::uint8_to_bool(leaf_eq + j*num_cmps + k*num_digits*num_cmps + m, temp_z, 8);
						}
					}
				}
				old_triple_count= triple_count;
			}

			clock_gettime(CLOCK_MONOTONIC, &finish);
			double total_time = (finish.tv_sec - start.tv_sec);
  		total_time += (finish.tv_nsec - start.tv_nsec) / 1000000000.0;
      std::cout<<"AND Time: "<<total_time<<std::endl;

			std::cout<<"Some Outputs"<< std::endl;

			/*for(int i=0; i<num_cmps; i++) {
				for(int j=0; j<batch_size; j++) {
					std::cout<<(int)leaf_eq[j*num_digits*num_cmps+i]<< " ";
				}
				std::cout<< std::endl;
			}*/

			//cleanup
			delete[] ei;
			delete[] fi;
			delete[] e;
			delete[] f;

		}

		void AND_step_1(uint8_t* ei, // evaluates batch of 8 ANDs
				uint8_t* fi,
				uint8_t* xi,
				uint8_t* yi,
				uint8_t* ai,
				uint8_t* bi,
				int num_ANDs) {
			assert(num_ANDs % 8 == 0);
			for(int i = 0; i < num_ANDs; i+=8) {
				ei[i/8] = ai[i/8];
				fi[i/8] = bi[i/8];
				ei[i/8] ^= sci::bool_to_uint8(xi+i, 8);
				fi[i/8] ^= sci::bool_to_uint8(yi+i, 8);
			}
		}
		void AND_step_2(uint8_t* zi, // evaluates batch of 8 ANDs
				uint8_t* e,
				uint8_t* f,
				uint8_t* ei,
				uint8_t* fi,
				uint8_t* ai,
				uint8_t* bi,
				uint8_t* ci,
				int num_ANDs)
		{
			assert(num_ANDs % 8 == 0);
			for(int i = 0; i < num_ANDs; i+=8) {
				uint8_t temp_z;
				if (party == sci::ALICE)
					temp_z = e[i/8] & f[i/8];
				else
					temp_z = 0;
				temp_z ^= f[i/8] & ai[i/8];
				temp_z ^= e[i/8] & bi[i/8];
				temp_z ^= ci[i/8];
				sci::uint8_to_bool(zi+i, temp_z, 8);
			}
		}
};



void computeLeafOTsThread(int party, string address, int port, BatchEqualitySplit<NetIO>* compare, uint64_t* x) {
	compare->io1 = new NetIO(party==1 ? nullptr:address.c_str(), port);
	compare->otpack1 = new OTPack<NetIO>(compare->io1, party, compare->beta, compare->l);
	compare->triple_gen1 = new TripleGenerator<NetIO>(party, compare->io1, compare->otpack1);
	uint64_t comm_sent = compare->io1->counter;
  compare->computeLeafOTs(x);
	comm_sent = compare->io1->counter - comm_sent;
	std::cout << "Leaf OTs Triples Comm. Sent/ell: " << comm_sent << std::endl;
}

void generate_triples_thread(int party, string address, int port, BatchEqualitySplit<NetIO>* compare) {
	compare->io2 = new NetIO(party==1 ? nullptr:address.c_str(), port+1);
	compare->otpack2 = new OTPack<NetIO>(compare->io2, 3-party, compare->beta, compare->l);
	compare->triple_gen2 = new TripleGenerator<NetIO>(3-party, compare->io2, compare->otpack2);
	uint64_t comm_sent = compare->io2->counter;
  compare->generate_triples();
	comm_sent = compare->io2->counter - comm_sent;
	std::cout << "Beaver Triples Comm. Sent/ell: " << comm_sent << std::endl;
}

void perform_batch_equality(uint64_t* inputs, int party, int num_cmps, int batch_size, string address, int port, uint8_t* res_shares) {
    int l=62, b=5;

    uint64_t mask_l;
    if (l == 64) mask_l = -1;
    else mask_l = (1ULL << l) - 1;

    std::cout << "All Base OTs Done" << std::endl;

  /*uint64_t comm_sent = 0;
	uint64_t multiThreadedIOStart[2];
	for(int i=0;i<2;i++){
		multiThreadedIOStart[i] = ioArr[i]->counter;
	}*/

  BatchEqualitySplit<NetIO>* compare;
  compare = new BatchEqualitySplit<NetIO>(party, l, b, batch_size, num_cmps);

    std::thread cmp_threads[2];
    cmp_threads[0] = std::thread(computeLeafOTsThread, party, address, port, compare, inputs);
    cmp_threads[1] = std::thread(generate_triples_thread, party, address, port, compare);

    for (int i = 0; i < 2; ++i) {
      cmp_threads[i].join();
    }

    compare->traverse_and_compute_ANDs();

      uint64_t comm_sent = compare->io1->counter + compare->io2->counter;
      std::cout << "Comm. Sent/ell: " << comm_sent << std::endl;

    /************** Verification ****************/
    /********************************************/
   /*
    switch (party) {
        case sci::ALICE: {
            ioArr[0]->send_data(x, 8*num_cmps);
            ioArr[0]->send_data(z, num_cmps);
            break;
        }
        case sci::BOB: {
            uint64_t *xi = new uint64_t[num_cmps];
            uint8_t *zi = new uint8_t[num_cmps];
            xi = new uint64_t[num_cmps];
            zi = new uint8_t[num_cmps];
            ioArr[0]->recv_data(xi, 8*num_cmps);
            ioArr[0]->recv_data(zi, num_cmps);
            for(int i = 0; i < num_cmps; i++) {
                zi[i] ^= z[i];
                assert(zi[i] == ((xi[i] & mask_l) > (x[i] & mask_l)));
            }
            cout << "Secure Comparison Successful" << endl;
            delete[] xi;
            delete[] zi;
            break;
        }
    }
    delete[] x;
    delete[] z;*/

    /**** Process & Write Benchmarking Data *****/
    /********************************************/
    /*
    string file_addr;
    switch (party) {
        case 1: {
            file_addr = "millionaire-P0.csv";
            break;
        }
        case 2: {
            file_addr = "millionaire-P1.csv";
            break;
        }
    }
    bool write_title = true; {
        fstream result(file_addr.c_str(), fstream::in);
        if(result.is_open())
            write_title = false;
        result.close();
    }
    fstream result(file_addr.c_str(), fstream::out|fstream::app);
    if(write_title){
        result << "Bitlen,Base,Batch Size,#Threads,#Comparisons,Time (mus),Throughput/sec" << endl;
    }
    result << l << "," << b << "," << batch_size << "," << num_threads << "," << num_cmps
        << "," << t << "," << (double(num_cmps)/t)*1e6 << endl;
    result.close();
    */
    /******************* Cleanup ****************/
    /********************************************/

}

#endif //BATCHEQUALITY_H__
