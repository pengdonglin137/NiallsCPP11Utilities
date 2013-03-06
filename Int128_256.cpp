/* Int128_256.hpp
(C) 2013 Niall Douglas http://www.nedprod.com
Created: Feb 2013

AVX2 accelerated 256 bit integer implementation. Falls back to SSE2/NEON 128 bit integers, or even char[32] if needed.

Literally combines cityhash 128 bit and spookyhash 128 bit to make a 256 bit hash
which isn't fast, but it's the fastest reasonably good 256 bit hash I can make quickly.
*/

#include "Int128_256.hpp"
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include "hashes/cityhash/src/city.cc"
#include "hashes/spookyhash/SpookyV2.cpp"
#include "hashes/sha256/sha256-ref.c"
#include "hashes/sha256/sha256-sse.c"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#ifdef _OPENMP
#include <omp.h>
#endif
#include <random>

#if ALLOW_UNALIGNED_READS
#error ALLOW_UNALIGNED_READS needs to be zero for ARM compatibility
#endif

namespace NiallsCPP11Utilities {

template<class generator_type> void FillRandom(char *buffer, size_t length)
{
	// No speed benefit so disabled
//#pragma omp parallel if(0 && length>=1024) num_threads((int)(length/256))
	{
#if 0 //def _OPENMP
		const int partitions=omp_get_num_threads();
		const int thispartition=omp_get_thread_num();
#else
		const int partitions=1;
		const int thispartition=0;
#endif
		const size_t thislength=(length/partitions)&~(sizeof(generator_type::result_type)-1);
		const size_t thisno=thislength/sizeof(generator_type::result_type);
		random_device rd;
		generator_type gen(rd());
		generator_type::result_type *tofill=(generator_type::result_type *) buffer;
		if(thispartition)
			tofill+=thisno*thispartition;
		for(size_t n=0; n<thisno; n++)
			*tofill++=gen();
		if(thispartition==partitions-1)
		{
			for(size_t remaining=length-(thislength*partitions); remaining>0; remaining-=sizeof(generator_type::result_type))
			{
				*tofill++=gen();
			}
		}
	}
}

void Int128::FillFastRandom(Int128 *ints, size_t no)
{
	// For integers, Mersenne is faster
	FillQualityRandom(ints, no);
	return;
	size_t length=no*sizeof(*ints);
	if(no && no!=length/sizeof(*ints)) abort();
#ifdef __LP64__
	typedef ranlux48_base generator_type;
#else
	typedef ranlux24_base generator_type;
#endif
	FillRandom<generator_type>((char *) ints, length);
}

void Int128::FillQualityRandom(Int128 *ints, size_t no)
{
	size_t length=no*sizeof(*ints);
	if(no && no!=length/sizeof(*ints)) abort();
#ifdef __LP64__
	typedef mt19937_64 generator_type;
#else
	typedef mt19937 generator_type;
#endif
	FillRandom<generator_type>((char *) ints, length);
}

void Int256::FillFastRandom(Int256 *ints, size_t no)
{
	// For integers, Mersenne is faster
	FillQualityRandom(ints, no);
	return;
	size_t length=no*sizeof(*ints);
	if(no && no!=length/sizeof(*ints)) abort();
#ifdef __LP64__
	typedef ranlux48_base generator_type;
#else
	typedef ranlux24_base generator_type;
#endif
	FillRandom<generator_type>((char *) ints, length);
}

void Int256::FillQualityRandom(Int256 *ints, size_t no)
{
	size_t length=no*sizeof(*ints);
	if(no && no!=length/sizeof(*ints)) abort();
#ifdef __LP64__
	typedef mt19937_64 generator_type;
#else
	typedef mt19937 generator_type;
#endif
	FillRandom<generator_type>((char *) ints, length);
}

void Hash128::AddFastHashTo(const char *data, size_t length)
{
	uint64 *spookyhash=const_cast<unsigned long long *>(asLongLongs());
	SpookyHash::Hash128(data, length, spookyhash, spookyhash+1);
}

void Hash128::BatchAddFastHashTo(size_t no, Hash128 *hashs, const char **data, size_t *length)
{
	// TODO: Implement a SIMD version of SpookyHash, and parallelise that too :)
#pragma omp parallel for
	for(size_t n=0; n<no; n++)
		hashs[n].AddFastHashTo(data[n], length[n]);
}


void Hash256::AddFastHashTo(const char *data, size_t length)
{
	uint64 *spookyhash=const_cast<unsigned long long *>(asLongLongs());
	uint128 cityhash=*(uint128 *)(asLongLongs()+2);
#pragma omp parallel for if(length>=1024)
	for(int n=0; n<2; n++)
	{
		if(!n)
			SpookyHash::Hash128(data, length, spookyhash, spookyhash+1);
		else
			cityhash=CityHash128WithSeed(data, length, cityhash);
	}
	*(uint128 *)(asLongLongs()+2)=cityhash;
}

void Hash256::BatchAddFastHashTo(size_t no, Hash256 *hashs, const char **data, size_t *length)
{
#pragma omp parallel for
	for(size_t n=0; n<no; n++)
		hashs[n].AddFastHashTo(data[n], length[n]);
}

void Hash256::AddSHA256To(const char *data, size_t length)
{
	const __sha256_block_t *blks=(const __sha256_block_t *) data;
	size_t no=length/sizeof(__sha256_block_t);
	size_t remaining=length-(no*sizeof(__sha256_block_t));
	for(size_t n=0; n<no; n++)
		__sha256_osol(*blks++, const_cast<unsigned int *>(asInts())); 
	if(remaining)
	{
		__sha256_block_t temp;
		memset(temp, 0, sizeof(temp));
		memcpy(temp, blks, remaining);
		__sha256_osol(temp, const_cast<unsigned int *>(asInts())); 
	}
}

void Hash256::BatchAddSHA256To(size_t no, Hash256 *hashs, const char **data, size_t *length)
{
	const __sha256_block_t *blks[4]={0};
	size_t lengths[4]={0};
	unsigned *out[4]={0};
	vector<tuple<Hash256 *, const char *, size_t>> tobefinished; // The hash and amount (less than one block) remaining. These need processing separately.
	tobefinished.reserve(no);
	bool done=false;
	while(!done)
	{
		// Fill SHA streams with work
		for(size_t n=0; n<4; n++)
		{
			if(!blks[n] && no)
			{
				blks[n]=(const __sha256_block_t *) *data++;
				out[n]=const_cast<unsigned int *>((*hashs++).asInts());
				lengths[n]=*length++;
				if(--no==(size_t)-1)
				{
					done=true;
					break;
				}
			}
			// Retire if length is too small
			if(blks[n] && lengths[n]<sizeof(__sha256_block_t))
			{
				if(lengths[n])
					tobefinished.push_back(make_tuple((Hash256 *) out[n], blks[n], lengths[n]));
				blks[n]=0;
				continue;
			}
		}
		if(done) break;
#ifndef NDEBUG
		for(size_t n=0; n<4; n++)
		{
			assert(blks[n]);
			assert(lengths[n]>=sizeof(__sha256_block_t));
		}
#endif
		__sha256_int(blks, out); 
		for(size_t n=0; n<4; n++)
		{
			blks[n]++;
			lengths[n]-=sizeof(__sha256_block_t));
		}
	}
	for(const auto &i : tobefinished)
	{
		__sha256_block_t temp;
		memset(temp, 0, sizeof(temp));
		memcpy(temp, get<1>(i), get<2>(i));
		__sha256_osol(temp, const_cast<unsigned int *>(get<0>(i)->asInts())); 
	}
}


} // namespace