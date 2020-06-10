// Copyright (c) 2018 Microsoft Corporation
// Licensed under the MIT license.
// Author: Paul Koch <code@koch.ninja>

#include "PrecompiledHeader.h"

#include <string.h> // memset
#include <stdlib.h> // malloc, realloc, free
#include <stddef.h> // size_t, ptrdiff_t

#include "EbmInternal.h" // EBM_INLINE & UNLIKLEY
#include "Logging.h" // EBM_ASSERT & LOG
#include "RandomStream.h" // our header didn't need the full definition, but we use the RandomStream in here, so we need it
#include "DataSetByFeatureCombination.h"
#include "SamplingSet.h"

SamplingSet::~SamplingSet() {
   LOG_0(TraceLevelInfo, "Entered ~SamplingSet");
   free(const_cast<size_t *>(m_aCountOccurrences));
   LOG_0(TraceLevelInfo, "Exited ~SamplingSet");
}

size_t SamplingSet::GetTotalCountInstanceOccurrences() const {
   // for SamplingSet (bootstrap sampling), we have the same number of instances as our original dataset
   size_t cTotalCountInstanceOccurrences = m_pOriginDataSet->GetCountInstances();
#ifndef NDEBUG
   size_t cTotalCountInstanceOccurrencesDebug = 0;
   for(size_t i = 0; i < m_pOriginDataSet->GetCountInstances(); ++i) {
      cTotalCountInstanceOccurrencesDebug += m_aCountOccurrences[i];
   }
   EBM_ASSERT(cTotalCountInstanceOccurrencesDebug == cTotalCountInstanceOccurrences);
#endif // NDEBUG
   return cTotalCountInstanceOccurrences;
}

SamplingSet * SamplingSet::GenerateSingleSamplingSet(
   RandomStream * const pRandomStream, 
   const DataSetByFeatureCombination * const pOriginDataSet
) {
   LOG_0(TraceLevelVerbose, "Entered SamplingSet::GenerateSingleSamplingSet");

   EBM_ASSERT(nullptr != pRandomStream);
   EBM_ASSERT(nullptr != pOriginDataSet);

   const size_t cInstances = pOriginDataSet->GetCountInstances();
   EBM_ASSERT(0 < cInstances); // if there were no instances, we wouldn't be called

   size_t * const aCountOccurrences = EbmMalloc<size_t>(cInstances);
   if(nullptr == aCountOccurrences) {
      LOG_0(TraceLevelWarning, "WARNING SamplingSet::GenerateSingleSamplingSet nullptr == aCountOccurrences");
      return nullptr;
   }

   try {
      for(size_t iInstance = 0; iInstance < cInstances; ++iInstance) {
         const size_t iCountOccurrences = pRandomStream->Next(cInstances);
         ++aCountOccurrences[iCountOccurrences];
      }
   } catch(...) {
      // pRandomStream->Next can throw exceptions from the random number generator, possibly (it's not documented)
      free(aCountOccurrences);
      LOG_0(TraceLevelWarning, "WARNING SamplingSet::GenerateSingleSamplingSet random number generator exception");
      return nullptr;
   }

   SamplingSet * pRet = new (std::nothrow) SamplingSet(pOriginDataSet, aCountOccurrences);
   if(nullptr == pRet) {
      LOG_0(TraceLevelWarning, "WARNING SamplingSet::GenerateSingleSamplingSet nullptr == pRet");
      free(aCountOccurrences);
      return nullptr;
   }

   LOG_0(TraceLevelVerbose, "Exited SamplingSet::GenerateSingleSamplingSet");
   return pRet;
}

SamplingSet * SamplingSet::GenerateFlatSamplingSet(const DataSetByFeatureCombination * const pOriginDataSet) {
   LOG_0(TraceLevelInfo, "Entered SamplingSet::GenerateFlatSamplingSet");

   // TODO: someday eliminate the need for generating this flat set by specially handling the case of no internal bagging
   EBM_ASSERT(nullptr != pOriginDataSet);
   const size_t cInstances = pOriginDataSet->GetCountInstances();
   EBM_ASSERT(0 < cInstances); // if there were no instances, we wouldn't be called

   const size_t cBytesData = sizeof(size_t) * cInstances;
   size_t * const aCountOccurrences = static_cast<size_t *>(malloc(cBytesData));
   if(nullptr == aCountOccurrences) {
      LOG_0(TraceLevelWarning, "WARNING SamplingSet::GenerateFlatSamplingSet nullptr == aCountOccurrences");
      return nullptr;
   }

   for(size_t iInstance = 0; iInstance < cInstances; ++iInstance) {
      aCountOccurrences[iInstance] = 1;
   }

   SamplingSet * pRet = new (std::nothrow) SamplingSet(pOriginDataSet, aCountOccurrences);
   if(nullptr == pRet) {
      LOG_0(TraceLevelWarning, "WARNING SamplingSet::GenerateFlatSamplingSet nullptr == pRet");
      free(aCountOccurrences);
   }

   LOG_0(TraceLevelInfo, "Exited SamplingSet::GenerateFlatSamplingSet");
   return pRet;
}

void SamplingSet::FreeSamplingSets(const size_t cSamplingSets, SamplingSet ** const apSamplingSets) {
   LOG_0(TraceLevelInfo, "Entered SamplingSet::FreeSamplingSets");
   if(LIKELY(nullptr != apSamplingSets)) {
      const size_t cSamplingSetsAfterZero = 0 == cSamplingSets ? 1 : cSamplingSets;
      for(size_t iSamplingSet = 0; iSamplingSet < cSamplingSetsAfterZero; ++iSamplingSet) {
         delete apSamplingSets[iSamplingSet];
      }
      free(apSamplingSets);
   }
   LOG_0(TraceLevelInfo, "Exited SamplingSet::FreeSamplingSets");
}

SamplingSet ** SamplingSet::GenerateSamplingSets(
   RandomStream * const pRandomStream, 
   const DataSetByFeatureCombination * const pOriginDataSet, 
   const size_t cSamplingSets
) {
   LOG_0(TraceLevelInfo, "Entered SamplingSet::GenerateSamplingSets");

   EBM_ASSERT(nullptr != pRandomStream);
   EBM_ASSERT(nullptr != pOriginDataSet);

   const size_t cSamplingSetsAfterZero = 0 == cSamplingSets ? 1 : cSamplingSets;

   SamplingSet ** apSamplingSets = EbmMalloc<SamplingSet *>(cSamplingSetsAfterZero);
   if(UNLIKELY(nullptr == apSamplingSets)) {
      LOG_0(TraceLevelWarning, "WARNING SamplingSet::GenerateSamplingSets nullptr == apSamplingSets");
      return nullptr;
   }
   if(0 == cSamplingSets) {
      // zero is a special value that really means allocate one set that contains all instances.
      SamplingSet * const pSingleSamplingSet = GenerateFlatSamplingSet(pOriginDataSet);
      if(UNLIKELY(nullptr == pSingleSamplingSet)) {
         LOG_0(TraceLevelWarning, "WARNING SamplingSet::GenerateSamplingSets nullptr == pSingleSamplingSet");
         free(apSamplingSets);
         return nullptr;
      }
      apSamplingSets[0] = pSingleSamplingSet;
   } else {
      for(size_t iSamplingSet = 0; iSamplingSet < cSamplingSets; ++iSamplingSet) {
         SamplingSet * const pSingleSamplingSet = GenerateSingleSamplingSet(pRandomStream, pOriginDataSet);
         if(UNLIKELY(nullptr == pSingleSamplingSet)) {
            LOG_0(TraceLevelWarning, "WARNING SamplingSet::GenerateSamplingSets nullptr == pSingleSamplingSet");
            FreeSamplingSets(cSamplingSets, apSamplingSets);
            return nullptr;
         }
         apSamplingSets[iSamplingSet] = pSingleSamplingSet;
      }
   }
   LOG_0(TraceLevelInfo, "Exited SamplingSet::GenerateSamplingSets");
   return apSamplingSets;
}