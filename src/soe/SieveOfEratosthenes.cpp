//
// Copyright (c) 2011 Kim Walisch, <kim.walisch@gmail.com>.
// All rights reserved.
//
// This file is part of primesieve.
// Visit: http://primesieve.googlecode.com
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//   * Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above
//     copyright notice, this list of conditions and the following
//     disclaimer in the documentation and/or other materials provided
//     with the distribution.
//   * Neither the name of the author nor the names of its
//     contributors may be used to endorse or promote products derived
//     from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.

#include "SieveOfEratosthenes.h"
#include "PreSieve.h"
#include "EratSmall.h"
#include "EratMedium.h"
#include "EratBig.h"
#include "defs.h"
#include "imath.h"

#include <stdexcept>
#include <cstdlib>
#include <algorithm>
#include <cassert>

const uint32_t SieveOfEratosthenes::bitValues_[32] = {
     7,  11,  13,  17,  19,  23,  29,  31, /* sieve_[i+0] */
    37,  41,  43,  47,  49,  53,  59,  61, /* sieve_[i+1] */
    67,  71,  73,  77,  79,  83,  89,  91, /* sieve_[i+2] */
    97, 101, 103, 107, 109, 113, 119, 121  /* sieve_[i+3] */
};

/**
 * @param startNumber   Sieve the primes within the interval [startNumber, stopNumber].
 * @param stopNumber    ...
 * @param sieveSize     A sieve size in bytes.
 * @param preSieveLimit Multiples of small primes <= preSieveLimit are
 *                      pre-sieved.
 */
SieveOfEratosthenes::SieveOfEratosthenes(uint64_t startNumber,
                                         uint64_t stopNumber,
                                         uint32_t sieveSize,
                                         uint32_t preSieveLimit) :
  startNumber_(startNumber), stopNumber_(stopNumber),
    sieve_(NULL), sieveSize_(sieveSize),
      isFirstSegment_(true), preSieve_(preSieveLimit),
        eratSmall_(NULL), eratMedium_(NULL), eratBig_(NULL)
  {
  if (startNumber_ < 7 || startNumber_ > stopNumber_)
    throw std::logic_error(
        "SieveOfEratosthenes: startNumber must be >= 7 && <= stop number.");
  // it makes no sense to use very small sieve sizes as a sieve
  // size of the CPU's L1 or L2 cache size performs best
  if (sieveSize_ < 1024)
    throw std::invalid_argument(
        "SieveOfEratosthenes: sieveSize must be >= 1024.");
  if (sieveSize_ > UINT32_MAX / NUMBERS_PER_BYTE)
    throw std::overflow_error(
        "SieveOfEratosthenes: sieveSize must be <= 2^32 / 30.");
  sieve_ = new uint8_t[sieveSize_];
  segmentLow_ = startNumber_ - this->getByteRemainder(startNumber_);
  assert(segmentLow_ % NUMBERS_PER_BYTE == 0);
  // '+ 1' is a correction for primes of type i*30 + 31
  segmentHigh_ = segmentLow_ + sieveSize_ * NUMBERS_PER_BYTE + 1;
  try {
    this->initEratAlgorithms();
  } catch (...) {
    delete[] sieve_;
    delete eratSmall_;
    delete eratMedium_;
    delete eratBig_;
    throw;
  }
}

SieveOfEratosthenes::~SieveOfEratosthenes() {
  delete[] sieve_;
  delete eratSmall_;
  delete eratMedium_;
  delete eratBig_;
}

uint32_t SieveOfEratosthenes::getPreSieveLimit() const {
  return preSieve_.getLimit();
}

uint32_t SieveOfEratosthenes::getByteRemainder(uint64_t n) const {
  uint32_t remainder = static_cast<uint32_t> (n % NUMBERS_PER_BYTE);
  // correction for primes of type i*30 + 31
  if (remainder <= 1)
    remainder += NUMBERS_PER_BYTE;
  return remainder;
}

void SieveOfEratosthenes::initEratAlgorithms() {
  uint32_t sqrtStop = isqrt(stopNumber_);
  uint32_t limit;
  if (preSieve_.getLimit() < sqrtStop) {
    limit = static_cast<uint32_t> (sieveSize_* defs::ERATSMALL_FACTOR);
    eratSmall_ = new EratSmall(std::min<uint32_t>(limit, sqrtStop), *this);
    if (eratSmall_->getLimit() < sqrtStop) {
      limit = static_cast<uint32_t> (sieveSize_* defs::ERATMEDIUM_FACTOR);
      eratMedium_ = new EratMedium(std::min<uint32_t>(limit, sqrtStop), *this);
      if (eratMedium_->getLimit() < sqrtStop)
        eratBig_ = new EratBig(*this);
    }
  }
}

/**
 * Pre-sieve multiples of small primes <= preSieve_.getLimit() to
 * speed up the sieve of Eratosthenes.
 */
void SieveOfEratosthenes::preSieve() {
  preSieve_.doIt(sieve_, sieveSize_, segmentLow_);
  if (isFirstSegment_) {
    isFirstSegment_ = false;
    // correct preSieve_.doIt() for numbers <= 23 
    if (startNumber_ <= preSieve_.getLimit())
      sieve_[0] = 0xff;
    uint32_t startRemainder = this->getByteRemainder(startNumber_);
    // unset the bits corresponding to numbers < startNumber_
    for (int i = 0; bitValues_[i] < startRemainder; i++)
      sieve_[0] &= ~(1 << i);
  }
}

/**
 * Cross-off the multiples within the current segment (i.e. the
 * interval [segmentLow_, segmentLow_ + (sieveSize_*30+1)]).
 */
void SieveOfEratosthenes::crossOffMultiples() {
  if (eratSmall_ != NULL) {
    eratSmall_->sieve(sieve_, sieveSize_);
    if (eratMedium_ != NULL) {
      eratMedium_->sieve(sieve_, sieveSize_);
      if (eratBig_ != NULL)
        eratBig_->sieve(sieve_);
    }
  }
}

/**
 * Implementation of the segmented sieve of Eratosthenes,
 * sieve(uint32_t) must be called consecutively for all primes
 * (> getPreSieveLimit()) up to sqrt(stopNumber_) in order to sieve
 * the primes within the interval [startNumber_, stopNumber_].
 */
void SieveOfEratosthenes::sieve(uint32_t prime) {
  assert(prime > this->getPreSieveLimit());
  assert(prime <= isqrt(stopNumber_));
  assert(eratSmall_ != NULL);
  uint64_t primeSquared = isquare(prime);

  // The following while loop segments the sieve of Eratosthenes,
  // it is entered if all sieving primes <= sqrt(segmentHigh_)
  // required to sieve the next segment have been stored in the erat*_
  // objects. Each while loop sieves the primes within the interval
  // [segmentLow_, segmentLow_ + (sieveSize_*30+1)].
  while (segmentHigh_ < primeSquared) {
    this->preSieve();
    this->crossOffMultiples();
    this->analyseSieve(sieve_, sieveSize_);
    segmentLow_ += sieveSize_ * NUMBERS_PER_BYTE;
    segmentHigh_ += sieveSize_ * NUMBERS_PER_BYTE;
  }
  // prime is added to eratSmall_ if it has many multiple occurrences
  // per segment.
  // prime is added to eratMedium_ if it has a few multiple
  // occurrences per segment.
  // prime is added to eratBig_ if it has less than one multiple
  // occurrence per segment.
  if (prime > eratSmall_->getLimit())
    if (prime > eratMedium_->getLimit())
            eratBig_->addSievingPrime(prime);
    else eratMedium_->addSievingPrime(prime);
  else    eratSmall_->addSievingPrime(prime);
}

/**
 * Sieve the last segments remaining after that sieve(uint32_t) has
 * been called for all primes up to sqrt(stopNumber_).
 */
void SieveOfEratosthenes::finish() {
  assert(segmentLow_ < stopNumber_);
  // sieve all segments left except the last one
  while (segmentHigh_ < stopNumber_) {
    this->preSieve();
    this->crossOffMultiples();
    this->analyseSieve(sieve_, sieveSize_);
    segmentLow_ += sieveSize_ * NUMBERS_PER_BYTE;
    segmentHigh_ += sieveSize_ * NUMBERS_PER_BYTE;
  }
  uint32_t stopRemainder = this->getByteRemainder(stopNumber_);
  // calculate the sieve size of the last segment
  sieveSize_ = static_cast<uint32_t> ((stopNumber_ - stopRemainder) 
      - segmentLow_) / NUMBERS_PER_BYTE + 1;
  // sieve the last segment
  this->preSieve();
  this->crossOffMultiples();
  // unset the bits corresponding to numbers > stopNumber_
  for (int i = 0; i < 8; i++) {
    if (bitValues_[i] > stopRemainder)
      sieve_[sieveSize_ - 1] &= ~(1 << i);
  }
  this->analyseSieve(sieve_, sieveSize_);
}
