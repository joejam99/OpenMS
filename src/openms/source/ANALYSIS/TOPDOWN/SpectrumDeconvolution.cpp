//
// Created by Kyowon Jeong on 9/25/19.
//

#include "OpenMS/ANALYSIS/TOPDOWN/SpectrumDeconvolution.h"

namespace OpenMS
{

  // constructor
  SpectrumDeconvolution::SpectrumDeconvolution(MSSpectrum &s, Parameter &p) :
      spec(s), param(p)
  {
    //averagines = a;
    setFilters();
    updateLogMzPeaks();
  }

  /// default destructor
  SpectrumDeconvolution::~SpectrumDeconvolution()
  {
    if (logMzPeaks.empty())
    {
      return;
    }
    std::vector<LogMzPeak>().swap(logMzPeaks);
    std::vector<PeakGroup>().swap(peakGroups);
    delete[] binOffsets;
    delete[] filter;

    for (Size k = 0; k < param.hCharges.size(); k++)
    {
      delete[] hBinOffsets[k];
      delete[] harmonicFilter[k];
    }
    delete[] hBinOffsets;
    delete[] harmonicFilter;
  }

  void SpectrumDeconvolution::setFilters()
  {

    filter = new double[param.chargeRange];
    harmonicFilter = new double *[param.hCharges.size()];
    //    int *random = new int[param.chargeRange];
    //std::srand(std::time(nullptr));

    for (int i = 0; i < param.chargeRange; i++)
    {
      filter[i] = log(
          1.0 / (i + param.minCharge));
    }

    for (Size k = 0; k < param.hCharges.size(); k++)
    {
      harmonicFilter[k] = new double[param.chargeRange];
      auto &hc = param.hCharges[k];
      float n = (float) (hc / 2);

      for (int i = 0; i < param.chargeRange; i++)
      {
        //auto r = ((float) rand()) / (float) RAND_MAX - .5;log(1.0 / (i + n / hc + param.minCharge));
        harmonicFilter[k][i] = log(1.0 / (i + n / hc + param.minCharge)); //.124 * (1 + i%2) + should be descending, and negative!  - 0.2 * (1+(i%3)/2.0)) // TODO
      }
    }
  }

  void SpectrumDeconvolution::updateLogMzPeaks()
  {
    logMzPeaks.reserve(spec.size());
    for (auto &peak: spec)
    {
      if (peak.getIntensity() <= param.intensityThreshold)//
      {
        continue;
      }
      LogMzPeak logMzPeak(peak);
      logMzPeaks.push_back(logMzPeak);
    }
  }


  double SpectrumDeconvolution::getBinValue(Size bin, double minV, double binWidth)
  {
    return minV + bin / binWidth;
  }

  Size SpectrumDeconvolution::getBinNumber(double v, double minV, double binWidth)
  {
    if (v < minV)
    {
      return 0;
    }
    return (Size) (((v - minV) * binWidth) + .5);

  }

  void SpectrumDeconvolution::pringMasses(boost::dynamic_bitset<> &massBins, double &minMass, double binWidth)
  {
    auto index = massBins.find_first();
    std::cout << "m=[";
    while (index != massBins.npos)
    {
      std::cout << exp(getBinValue(index, minMass, binWidth)) << ",";
      index = massBins.find_next(index);
    }

    std::cout << "];" << std::endl;
  }

  void SpectrumDeconvolution::updateMzBins(double &mzBinMinValue, Size &binNumber, double binWidth,
                                           float *mzBinIntensities
  )
  {
    mzBins = boost::dynamic_bitset<>(binNumber);
    // double *intensities = new double[binNumber];
    std::fill_n(mzBinIntensities, binNumber, .0);

    for (auto &p : logMzPeaks)
    {
      Size bi = getBinNumber(p.logMz, mzBinMinValue, binWidth);
      if (bi >= binNumber)
      {
        continue;
      }
      mzBins.set(bi);
      mzBinIntensities[bi] += p.intensity;

      auto delta = (p.logMz - getBinValue(bi, mzBinMinValue, binWidth));

      if (delta > 0)
      {
        if (bi < binNumber - 1)
        {
          mzBins.set(bi + 1);
          mzBinIntensities[bi + 1] += p.intensity;
        }
      }
      else if (delta < 0)
      {
        if (bi > 0)
        {
          mzBins.set(bi - 1);
          mzBinIntensities[bi - 1] += p.intensity;
        }
      }



    }

    /*
    if (spec.getRT() < 3582.10 && spec.getRT() > 3582.08)
    {
      Size pbi = 0;
      for (auto &p : logMzPeaks)
      {
        Size bi = getBinNumber(p.logMz, mzBinMinValue, binWidth);
        if (p.mz > 740 && p.mz < 742)
        { //
          if (pbi != bi)
          {
            std::cout << bi << " " << p.mz << " " << mzBinIntensities[bi] << "\n";
            pbi = bi;
          }
        }
      }
      std::cout << std::endl;
    }*/
  }

  void SpectrumDeconvolution::unionPrevMassBins(double &massBinMinValue,
                                                std::vector<std::vector<Size>> &prevMassBinVector,
                                                std::vector<double> &prevMassBinMinValue,
                                                UInt msLevel)
  {
    //return;
    if (massBins.empty())
    {
      return;
    }
    for (Size i = 0; i < prevMassBinVector.size(); i++)
    {
      auto &pmb = prevMassBinVector[i];
      if (pmb.empty())
      {
        continue;
      }
      long shift = (long) (round((massBinMinValue - prevMassBinMinValue[i]) * param.binWidth[msLevel - 1]));

      for (auto &index : pmb)
      {
        long j = (long) index - shift;
        if (j < 0)
        {
          continue;
        }
        if ((Size) j >= massBins.size())
        {
          break;
        }
        massBins[j] = true;
      }
    }
  }


  boost::dynamic_bitset<> SpectrumDeconvolution::getCandidateMassBinsForThisSpectrum(float *massIntensitites,
                                                                                     float *mzIntensities,
                                                                                     double &mzMinValue,
                                                                                     unsigned int &msLevel)
  {
    int chargeRange = param.currentChargeRange;
    int hChargeSize = msLevel == 1 ? (int) param.hCharges.size() : 1;
    int minContinuousChargePeakCount = param.minContinuousChargePeakCount[msLevel - 1];

    long binEnd = (long) massBins.size();
    auto candidateMassBinsForThisSpectrum = boost::dynamic_bitset<>(massBins.size());

    Byte *continuousChargePeakPairCount = new Byte[massBins.size()];
    std::fill_n(continuousChargePeakPairCount, massBins.size(), 0);

    //double th = 1;//
    auto mzBinIndex = mzBins.find_first();
    std::fill_n(massIntensitites, massBins.size(), 0);

    Byte *prevCharges = new Byte[massBins.size()];
    std::fill_n(prevCharges, massBins.size(), (Byte) (chargeRange + 2));

    auto *prevIntensities = new float[massBins.size()];
    std::fill_n(prevIntensities, massBins.size(), 1);

    auto noise = new float *[hChargeSize + 1];
    for (auto k = 0; k < hChargeSize + 1; k++)
    {
      noise[k] = new float[massBins.size()];
      std::fill_n(noise[k], massBins.size(), 0);
    }
    auto binWidth = param.binWidth[msLevel - 1];

    float factor = 4.0;
    while (mzBinIndex != mzBins.npos)
    {
      auto &intensity = mzIntensities[mzBinIndex];
      double mz=-1.0, logMz = 0;

      for (Byte j = 0; j < chargeRange; j++)
      {
        long massBinIndex = mzBinIndex + binOffsets[j];
        // int charge = j + param.minCharge;
        //int thershold = msLevel ==1 ? minContinuousChargePeakCount : (int)(charge/2);
        //thershold = minContinuousChargePeakCount < thershold? minContinuousChargePeakCount : thershold;

        if (massBinIndex < 0)
        {
          continue;
        }
        if (massBinIndex >= binEnd)
        {
          break;
        }


        //auto cd = prevCharges[massBinIndex] - j;
        auto &prevIntensity = prevIntensities[massBinIndex];
        float minInt = std::min(intensity, prevIntensity);
        float maxInt = std::max(intensity, prevIntensity);

        float id = maxInt / minInt;
        bool out = prevCharges[massBinIndex] - j != 1;
        // auto lowChargePeak = prevCharges[massBinIndex] > chargeRange && j + param.minCharge <= 10;//
        // if (lowChargePeak) out = false; //

        // if (charge == 1){
        //   candidateMassBinsForThisSpectrum[massBinIndex] = true;
        // }
        auto charge = j + param.minCharge;
        //if (msLevel > 1)// &&
        {
          if(mz<=0){
            logMz = getBinValue(mzBinIndex, mzMinValue, binWidth);
            mz = exp(logMz);
          }
          double diff = Constants::ISOTOPE_MASSDIFF_55K_U / charge/ mz;

          //  double logM = getBinValue(massBinIndex, massBinMinValue, binWidth);
          //      double mass = exp(logM);
          //      if (msLevel == 1)
          //      {
          //
          //        double isoLogM1 = logM - diff;
          //        double isoLogM2 = logM + diff;
          //
          //        auto b1 = getBinNumber(isoLogM1, massBinMinValue, binWidth);


          auto nextIsoMz = logMz + diff;//log(mz + Constants::C13C12_MASSDIFF_U / charge);

          auto nextIsoBin = getBinNumber(nextIsoMz, mzMinValue, binWidth);

          if (nextIsoBin < mzBins.size() && mzBins[nextIsoBin] && intensity > mzIntensities[nextIsoBin] )
          {
            //continuousChargePeakPairCount[massBinIndex]++; //
            candidateMassBinsForThisSpectrum[massBinIndex] =
                (++continuousChargePeakPairCount[massBinIndex] >= minContinuousChargePeakCount);//
            massIntensitites[massBinIndex] += mzIntensities[nextIsoBin];
          //  candidateMassBinsForThisSpectrum[massBinIndex] = true;//
          }

//          auto waterAddMz = log(mz + 18.010565 / charge); // 17.031
//          auto waterAddBin = getBinNumber(waterAddMz, mzMinValue, binWidth);
//          if (waterAddBin < mzBins.size() && mzBins[waterAddBin]){
//            continuousChargePeakPairCount[massBinIndex]++; //
//          }
        }

        if (prevCharges[massBinIndex] < chargeRange && out && id < factor)
        {
          noise[hChargeSize][massBinIndex] += minInt;
        }

        if (out || id > factor) // TODO + per peak charge ?
        {
          //if (msLevel == 1){
          continuousChargePeakPairCount[massBinIndex] = 0;
          //}
        }
        else
        {
          int maxHcharge = -1;
          float maxHint = .0;
          //auto charge = j + param.minCharge;
          //auto currentMaxCharge = j + param.currentChargeRange;

          for (auto k = 0; k < hChargeSize; k++)
          {
            auto hmzBinIndex = massBinIndex - hBinOffsets[k][j];
            if (hmzBinIndex > 0 && hmzBinIndex < (long) mzBins.size() && mzBins[hmzBinIndex])
            {
              auto &hintensity = mzIntensities[hmzBinIndex];
              if (hintensity > minInt
                  && hintensity < factor * maxInt)
              {
                if (hintensity < maxHint)
                {
                  continue;
                }
                maxHint = hintensity;
                maxHcharge = k;
              }
            }

            /*
            if (spec.getRT() < 3582.10 && spec.getRT() > 3582.08)
            {
              if(mzBinIndex ==30688){
                std::cout<<mzBinIndex << " * " << maxHcharge << " " << massIntensitites[massBinIndex] << " " << (int) j << std::endl; //
                //std::cout<<mzBinIndex << " " << maxIndex << " " << (int) charge << std::endl; //
              }
            }*/


          }



          if (maxHcharge >= 0) //
          {
            noise[maxHcharge][massBinIndex] += maxHint;
            continuousChargePeakPairCount[massBinIndex] = 0;
          }
          else
          {
            massIntensitites[massBinIndex] += intensity;
            if (!candidateMassBinsForThisSpectrum[massBinIndex])
            {
              /*if (false) //
              { //
                auto charge = j + param.minCharge;
                auto binWidth = param.binWidth[msLevel-1];
                auto mz = exp(getBinValue(mzBinIndex, mzMinValue, binWidth));

                auto waterLossMz = log(mz - 18.010565 / charge); // 17.031
                auto nh3LossMz = log(mz - 17.026549 / charge);
                auto waterAddMz = log(mz + 18.010565 / charge); // 17.031
                auto nh3AddMz = log(mz + 17.026549 / charge);

                auto waterLossBin = getBinNumber(waterLossMz, mzMinValue, binWidth);
                auto nh3LossBin = getBinNumber(nh3LossMz, mzMinValue, binWidth);
                auto waterAddBin = getBinNumber(waterAddMz, mzMinValue, binWidth);
                auto nh3AddBin = getBinNumber(nh3AddMz, mzMinValue, binWidth);

                float lossIntensity = .0;
                if (waterLossBin>=0 && mzBins[waterLossBin]){
                  lossIntensity = mzIntensities[waterLossBin];
                }
                if(nh3LossBin>=0 && mzBins[nh3LossBin])
                {
                  lossIntensity = std::max(lossIntensity, mzIntensities[nh3LossBin]);
                }
                if (waterAddBin < mzBins.size() && mzBins[waterAddBin]){
                  lossIntensity = std::max(lossIntensity, mzIntensities[waterAddBin]);
                }
                if(nh3AddBin < mzBins.size() && mzBins[nh3AddBin])
                {
                  lossIntensity = std::max(lossIntensity, mzIntensities[nh3AddBin]);
                }

                if (lossIntensity > 0 && lossIntensity < intensity * factor){
                  massIntensitites[massBinIndex] += lossIntensity;
                  ++continuousChargePeakPairCount[massBinIndex];
                }
              }*/
              //++continuousChargePeakPairCount[massBinIndex];

              candidateMassBinsForThisSpectrum[massBinIndex] =
                  (++continuousChargePeakPairCount[massBinIndex] >= minContinuousChargePeakCount);//
            }
          }
        }




        prevIntensity = intensity;
        prevCharges[massBinIndex] = j;
      }




      mzBinIndex = mzBins.find_next(mzBinIndex);
    }
    auto mindex = candidateMassBinsForThisSpectrum.find_first();
    while (mindex != candidateMassBinsForThisSpectrum.npos)
    {
      auto &s = massIntensitites[mindex];
      // auto msnr = s / (noise[0][mindex]);
      float maxNoise = .0;
      for (auto k = 0; k <= hChargeSize; k++)
      {
        maxNoise = std::max(maxNoise, noise[k][mindex]);
        // msnr = min(snr, msnr);
      }
      s -= maxNoise;
      mindex = candidateMassBinsForThisSpectrum.find_next(mindex);
    }

    delete[] prevIntensities;
    delete[] prevCharges;
    for (auto k = 0; k <= hChargeSize; k++)
    {
      delete[] noise[k];
    }
    delete[] noise;
    delete[] continuousChargePeakPairCount;

    return candidateMassBinsForThisSpectrum;
  }


  Byte **SpectrumDeconvolution::updateMassBins_(boost::dynamic_bitset<> &candidateMassBinsForThisSpectrum,
                                                float *massIntensities,
                                                long &binStart, long &binEnd,
                                                unsigned int &msLevel
  )
  {
    int chargeRange = param.currentChargeRange;
    // auto binWidth = param.binWidth;
    Byte *maxChargeRanges = new Byte[massBins.size()];
    std::fill_n(maxChargeRanges, massBins.size(), 0);

    Byte *minChargeRanges = new Byte[massBins.size()];
    std::fill_n(minChargeRanges, massBins.size(), chargeRange + 1);

    Byte *mzChargeRanges = new Byte[mzBins.size()];
    std::fill_n(mzChargeRanges, mzBins.size(), chargeRange + 1);

    auto mzBinIndex = mzBins.find_first();
    long binSize = (long) massBins.size();

    massBinsForThisSpectrum = boost::dynamic_bitset<>(massBins.size());
    auto toSkip = (candidateMassBinsForThisSpectrum | massBins).flip();
    massBins.reset();
    //massBinsForThisSpectrum.reset();

    while (mzBinIndex != mzBins.npos)
    {
      long maxIndex = -1;
      float maxCount = -1e11;
      Byte charge = 0;

      for (Byte j = 0; j < chargeRange; j++)
      {
        long massBinIndex = mzBinIndex + binOffsets[j];
        if (massBinIndex < 0)
        {
          continue;
        }
        if (massBinIndex >= binSize)
        {
          break;
        }

        if (toSkip[massBinIndex])
        {
          continue;
        }

        auto &t = massIntensities[massBinIndex];// + noneContinuousChargePeakPairCount[massBinIndex];//

        /*if (spec.getRT() < 3582.10 && spec.getRT() > 3582.08)
        {
          if(mzBinIndex ==30688){
            std::cout<<mzBinIndex << " " << t << " " << (int) j << std::endl; //
            //std::cout<<mzBinIndex << " " << maxIndex << " " << (int) charge << std::endl; //
          }
        }
*/


        if (t == 0)
        { // no signal
          continue;
        }
        if (maxCount < t)
        {
          maxCount = t;
          maxIndex = massBinIndex;
          charge = j;
        }
      }

      if (maxIndex > binStart && maxIndex < binEnd)
      {


        {
          maxChargeRanges[maxIndex] = std::max(maxChargeRanges[maxIndex], charge);
          minChargeRanges[maxIndex] = std::min(minChargeRanges[maxIndex], charge);
          massBinsForThisSpectrum[maxIndex] = candidateMassBinsForThisSpectrum[maxIndex];
          mzChargeRanges[mzBinIndex] = charge;//minChargeRanges[maxIndex];//...
          massBins[maxIndex] = true;
        }
      }

      mzBinIndex = mzBins.find_next(mzBinIndex);
    }

    Byte **chargeRanges = new Byte *[3];
    chargeRanges[0] = minChargeRanges;
    chargeRanges[1] = maxChargeRanges;
    chargeRanges[2] = mzChargeRanges;
    // delete[] selected;
    return chargeRanges;
  }


  Byte **SpectrumDeconvolution::updateMassBins(double &massBinMinValue,
                                               double &mzBinMinValue,
                                               float *massIntensities,
                                               float *mzIntensities,
                                               unsigned int &msLevel
  )
  {
    auto binWidth = param.binWidth[msLevel - 1];
    long binThresholdMinMass = (long) getBinNumber(log(param.minMass), massBinMinValue, binWidth);
    long binThresholdMaxMass = (long) std::min(massBins.size(),
                                               1 + getBinNumber(log(param.currentMaxMass),
                                                                massBinMinValue,
                                                                binWidth));
    //auto y = 1;


    auto candidateMassBins = getCandidateMassBinsForThisSpectrum(massIntensities,
                                                                 mzIntensities,
                                                                 mzBinMinValue,
                                                                 msLevel);

   // if (spec.getRT() < 3582.10 && spec.getRT() > 3582.08)
    {

      //pringMasses(candidateMassBins, massBinMinValue,  binWidth); // TODO
    }

    auto perMassChargeRanges = updateMassBins_(candidateMassBins,
                                               massIntensities,
                                               binThresholdMinMass,
                                               binThresholdMaxMass,
                                               msLevel);

    //if (spec.getRT() < 3582.10 && spec.getRT() > 3582.08)
    {
      // pringMasses(massBins, massBinMinValue,  binWidth); // TODO
    }

    //if(msLevel>1)
    // pringMasses(massBins, massBinMinValue,  binWidth);

    return perMassChargeRanges;
  }

  /*void SpectrumDeconvolution::getCandidatePeakGroups(double &mzBinMinValue, double &massBinMinValue,
                                                       float *massIntensities,
                                                       Byte **chargeRanges,
                                                       FLASHDeconvHelperStructs::PrecalcularedAveragine &avg
    )
    {
      double binWidth = param.binWidth;
      double tol = param.tolerance * 2;
      int minCharge = param.minCharge;
      int chargeRange = param.chargeRange;
      int maxIsotopeCount = param.maxIsotopeCount;

      int logMzPeakSize = (int) logMzPeaks.size();
      Size massBinSize = massBins.size();
      int *currentPeakIndex = new int[param.chargeRange];
      std::fill_n(currentPeakIndex, param.chargeRange, 0);

      peakGroups.reserve(massBins.count());
      auto &minChargeRanges = chargeRanges[0];
      auto &maxChargeRanges = chargeRanges[1];
      auto &mzChargeRanges = chargeRanges[2];

      auto massBinIndex = massBins.find_first();
      // Size lastSetMassBinIndex = unionedMassBins.size();
      Size *peakBinNumbers = new Size[logMzPeakSize];
      for (int i = 0; i < logMzPeakSize; i++)
      {
        peakBinNumbers[i] = getBinNumber(logMzPeaks[i].logMz, mzBinMinValue, binWidth);
      }

      while (massBinIndex != massBins.npos)
      {
        double logM = getBinValue(massBinIndex, massBinMinValue, binWidth);
        double diff = Constants::C13C12_MASSDIFF_U / exp(logM);
        double isoLogM1 = logM - diff;
        double isoLogM2 = logM + diff;

        auto b1 = getBinNumber(isoLogM1, massBinMinValue, binWidth);
        if (b1 > 0)
        {
          if (massIntensities[massBinIndex] < massIntensities[b1])
          {
            massBinIndex = massBins.find_next(massBinIndex);
            continue;
          }
        }

        auto b2 = getBinNumber(isoLogM2, massBinMinValue, binWidth);
        if (b2 < massBins.size())
        {
          if (massIntensities[massBinIndex] < massIntensities[b2])
          {
            massBinIndex = massBins.find_next(massBinIndex);
            continue;
          }
        }

        if (massIntensities[b1] == 0 && massIntensities[b2] == 0)
        {
          massBinIndex = massBins.find_next(massBinIndex);
          continue;
        }

        int isoOff = 0;
        PeakGroup pg;
        pg.reserve(chargeRange * 30);

        for (auto j = minChargeRanges[massBinIndex]; j <= maxChargeRanges[massBinIndex]; j++)
        {
          long &binOffset = binOffsets[j];
          auto bi = massBinIndex - binOffset;
          if (mzChargeRanges[bi] < chargeRange && mzChargeRanges[bi] != j)
          {
            continue;
          }

          int charge = j + minCharge;
          auto &cpi = currentPeakIndex[j];
          double maxIntensity = 0.0;
          int maxIntensityPeakIndex = -1;

          while (cpi < logMzPeakSize - 1)
          {
            //auto bi = peakBinNumbers[cpi] + binOffset;
            if (peakBinNumbers[cpi] == bi)
            {
              auto intensity = logMzPeaks[cpi].intensity;
              if (intensity > maxIntensity)
              {
                maxIntensity = intensity;
                maxIntensityPeakIndex = cpi;
              }
            }
            else if (peakBinNumbers[cpi] > bi)
            {
              break;
            }
            cpi++;
          }

          if (maxIntensityPeakIndex >= 0)
          {
            const double mz = logMzPeaks[maxIntensityPeakIndex].mz;
            //double &logMz = logMzPeaks[maxIntensityPeakIndex].logMz;
            const double isof = Constants::C13C12_MASSDIFF_U / charge;
            double mzDelta = tol * mz;
            //cout<<mzDelta<<endl;
            int maxI = 0;
            for (int d = -1; d <= 1; d += 2)
            { // negative then positive direction.
              int peakIndex = maxIntensityPeakIndex + (d < 0 ? d : 0);
              int lastPeakIndex = -100;
              for (int i = 0; i < maxIsotopeCount && (peakIndex >= 0 && peakIndex < logMzPeakSize); i++)
              {
                maxI = std::max(maxI, i);
                const double centerMz = mz + isof * i * d;
                const double centerMzMin = centerMz - mzDelta;
                const double centerMzMax = centerMz + mzDelta;
                bool isotopePeakPresent = false;
                if (lastPeakIndex >= 0)
                {
                  peakIndex = lastPeakIndex;
                }//maxIntensityPeakIndex + (d < 0 ? d : 0);
                for (; peakIndex >= 0 && peakIndex < logMzPeakSize; peakIndex += d)
                {
                  const double observedMz = logMzPeaks[peakIndex].mz;
                  if (observedMz < centerMzMin)
                  {
                    if (d < 0)
                    {
                      break;
                    }
                    else
                    {
                      continue;
                    }
                  }
                  if (observedMz > centerMzMax)
                  {
                    if (d < 0)
                    {
                      continue;
                    }
                    else
                    {
                      break;
                    }
                  }

                  isotopePeakPresent = true;
                  if (peakIndex != lastPeakIndex)
                  {
                    // auto &mzBin = ;
                    const auto bin = peakBinNumbers[peakIndex] + binOffset;

                    if (bin < massBinSize) //
                    {
                      LogMzPeak p(logMzPeaks[peakIndex], charge, i * d);
                      pg.peaks.push_back(p);
                      lastPeakIndex = peakIndex;
                    }
                  }
                }
                if (!isotopePeakPresent)
                {
                  break;
                }
              }//115814 88,29
            }


            //int minIsoIndex = maxI;
            for (auto &p : pg.peaks)
            {// assign the nearest isotope index..
              if (p.charge != charge)
              {
                continue;
              }
              for (int d = -1; d <= 1; d += 2)
              { // negative then positive direction.
                int maxId = 0;
                double minMzDelta = maxI;

                for (int i = 0; i <= maxI; i++)
                {
                  double centerMz = mz + isof * (p.isotopeIndex + i * d);
                  double delta = abs(centerMz - p.mz);

                  if (delta > minMzDelta)
                  {
                    break;
                  }
                  maxId = i * d;
                  minMzDelta = delta;
                }
                //if (maxId != 0 )cout<< maxId <<endl;
                p.isotopeIndex += maxId;
              }
              isoOff = std::min(isoOff, p.isotopeIndex);
            }

          }
        }
        if (!pg.peaks.empty())
        {
          int minIi = 10000;
          int maxIi = -10000;
          for (auto &p : pg.peaks)
          {
            minIi = std::min(minIi, p.isotopeIndex);
            maxIi = std::max(maxIi, p.isotopeIndex);
            p.isotopeIndex -= isoOff;
          }
          if (minIi != maxIi)
          {

            //pg.updateMassesAndIntensity();
            pg.massBinIndex = massBinIndex;
            peakGroups.push_back(pg);
          }
        }

        massBinIndex = massBins.find_next(massBinIndex);
      }
      delete[] currentPeakIndex;
      delete[] peakBinNumbers;
    }*/

  void SpectrumDeconvolution::getCandidatePeakGroups(double &mzBinMinValue, double &massBinMinValue,
                                                     float *massIntensities,
                                                     Byte **chargeRanges,
                                                     FLASHDeconvHelperStructs::PrecalcularedAveragine &avg,
                                                     unsigned int &msLevel
  )
  {
    auto maxMissingIsotope = 2;//msLevel == 1 ? 2 : 1;
    double binWidth = param.binWidth[msLevel - 1];
    double tol = param.tolerance[msLevel - 1];
    int minCharge = param.minCharge;
    int chargeRange = param.currentChargeRange;
    auto mzBinSize = mzBins.size();
    auto massBinSize = massBins.size();
    int logMzPeakSize = (int) logMzPeaks.size();
    int *currentPeakIndex = new int[chargeRange];
    std::fill_n(currentPeakIndex, chargeRange, 0);

    peakGroups.reserve(massBins.count());
    auto &minChargeRanges = chargeRanges[0];
    auto &maxChargeRanges = chargeRanges[1];
    auto &mzChargeRanges = chargeRanges[2];
    auto massBinIndex = massBins.find_first();
    Size *peakBinNumbers = new Size[logMzPeakSize];

    for (int i = 0; i < logMzPeakSize; i++)
    {
      peakBinNumbers[i] = getBinNumber(logMzPeaks[i].logMz, mzBinMinValue, binWidth);
    }

    while (massBinIndex != massBins.npos)
    {
      double logM = getBinValue(massBinIndex, massBinMinValue, binWidth);
      double mass = exp(logM);
      if (msLevel == 1)
      {
        double diff = Constants::ISOTOPE_MASSDIFF_55K_U / mass;
        double isoLogM1 = logM - diff;
        double isoLogM2 = logM + diff;

        auto b1 = getBinNumber(isoLogM1, massBinMinValue, binWidth);

        if (b1 > 0 && b1 < massBinIndex)
        {
          if (massIntensities[massBinIndex] < massIntensities[b1])
          {
            massBinIndex = massBins.find_next(massBinIndex);
            continue;
          }
        }
        auto b2 = getBinNumber(isoLogM2, massBinMinValue, binWidth);

        if (b2 < massBinSize && b2 > massBinIndex)
        {
          if (massIntensities[massBinIndex] < massIntensities[b2])
          {
            massBinIndex = massBins.find_next(massBinIndex);
            continue;
          }
        }

        if (massIntensities[b1] == 0 && massIntensities[b2] == 0) //
        {
          massBinIndex = massBins.find_next(massBinIndex);
          continue;
        }
      }

      PeakGroup pg;
      pg.perChargeSNR = new float[chargeRange];
      std::fill_n(pg.perChargeSNR, chargeRange, 0);

      pg.reserve(chargeRange * 30);

      Size rightIndex = avg.getRightIndex(mass);
      Size leftIndex = avg.getLeftIndex(mass);
      //auto perChargeNoisePower = new float[chargeRange];

      for (auto j = minChargeRanges[massBinIndex]; j <= maxChargeRanges[massBinIndex]; j++)
      {
        long &binOffset = binOffsets[j];
        auto bi = massBinIndex - binOffset;

        if (bi >= mzBinSize || (mzChargeRanges[bi] < chargeRange && mzChargeRanges[bi] != j))
        {
          continue;
        }

        double maxIntensity = -1.0;
        //double maxIntensityMass = -1;
        int charge = j + minCharge;
        auto &cpi = currentPeakIndex[j];
        int maxPeakIndex = -1;

        while (cpi < logMzPeakSize - 1)
        {
          if (peakBinNumbers[cpi] == bi)
          {
            auto intensity = logMzPeaks[cpi].intensity;
            if (intensity > maxIntensity)
            {
              maxIntensity = intensity;
              maxPeakIndex = cpi;
            }

          }
          else if (peakBinNumbers[cpi] > bi)
          {
            break;
          }
          cpi++;
        }

        if (maxPeakIndex < 0)
        {
          continue;
        }

        const double mz = logMzPeaks[maxPeakIndex].mz;
        const double isof = Constants::ISOTOPE_MASSDIFF_55K_U / charge;
        double mzDelta = tol * mz;

        auto &np = pg.perChargeSNR[j];
        //np = 0;

        int pi = 0;
        // int peakcntr = 0;
        for (int peakIndex = maxPeakIndex; peakIndex < logMzPeakSize; peakIndex++)
        {
          const double observedMz = logMzPeaks[peakIndex].mz;
          const double intensity = logMzPeaks[peakIndex].intensity;
          //observedMz = mz + isof * i * d - d * mzDelta;
          double di = observedMz - mz;

          int i = (int) (.5 + di / isof);

          if (i > (int) rightIndex)
          {
            break;
          }

          if (i - pi > maxMissingIsotope)
          {
            break;
          }

          if (abs(di - i * isof) >= mzDelta) // noise
          {
            np += intensity * intensity;
            //peakcntr++;
          }
          else
          {

            // peakcntr++;
            //  std::cout<< i << " + "<< peakIndex<< " " << (observedMz - mz) * charge << std::endl;
            const auto bin = peakBinNumbers[peakIndex] + binOffset;

            if (bin < massBinSize)
            {
              LogMzPeak p(logMzPeaks[peakIndex], charge, 0);
              pg.peaks.push_back(p);
              //massBins[bin] = false;
              //isoOff = std::min(isoOff, p.isotopeIndex);
              //lastPeakIndex = peakIndex;
              // nextMassBinIndex = nextMassBinIndex < bin? bin : nextMassBinIndex; // take max
            }
            pi = i;
          }

          //double snr = sp-np;// / (np + 1);

        }

        pi = 0;
        // int isocntr = pi;

        for (int peakIndex = maxPeakIndex - 1; peakIndex >= 0; peakIndex--)
        {
          const double observedMz = logMzPeaks[peakIndex].mz;
          const double intensity = logMzPeaks[peakIndex].intensity;

          //observedMz = mz + isof * i * d - d * mzDelta;
          double di = mz - observedMz;
          int i = (int) (.5 + di / isof);

          if (i > (int) leftIndex)
          {
            break;
          }

          if (i - pi > maxMissingIsotope)
          {
            break;
          }

          if (abs(di - i * isof) >= mzDelta)
          {
            np += intensity * intensity;
            //continue;
          }
          else
          {
            const auto bin = peakBinNumbers[peakIndex] + binOffset;

            if (bin < massBinSize)
            {
              LogMzPeak p(logMzPeaks[peakIndex], charge, 0);
              pg.peaks.push_back(p);
            }

            pi = i;
          }

          //double snr = sp-np;// / (np + 1);

        }
        //perChargeSNR[j] = np;
      }

      if (!pg.peaks.empty())
      {
        double maxIntensity = -1.0;
        double maxMass = .0;
        auto newPeaks = std::vector<LogMzPeak>();
        newPeaks.reserve(pg.peaks.size());
        for (auto &p : pg.peaks)
        {
          if (maxIntensity < p.intensity)
          {
            maxIntensity = p.intensity;
            maxMass = p.getUnchargedMass();
          }
        }
        double isoDelta = tol * maxMass;
        int minOff = 10000;
        for (auto &p : pg.peaks)
        {
          p.isotopeIndex = round((p.getUnchargedMass() - maxMass) / Constants::ISOTOPE_MASSDIFF_55K_U);
          if (abs(maxMass - p.getUnchargedMass() + Constants::ISOTOPE_MASSDIFF_55K_U * p.isotopeIndex) > isoDelta)
          {
            continue;
          }
          newPeaks.push_back(p);
          minOff = minOff > p.isotopeIndex ? p.isotopeIndex : minOff;
        }

        pg.peaks.swap(newPeaks);
        std::vector<LogMzPeak>().swap(newPeaks);

        for (auto &p : pg.peaks)
        {
          p.isotopeIndex -= minOff;
        }

        pg.massBinIndex = massBinIndex;
        //pg.perChargeSNR = perChargeNoisePower;
        peakGroups.push_back(pg); //
      }
      massBinIndex = massBins.find_next(massBinIndex);
    }
    delete[] currentPeakIndex;
    delete[] peakBinNumbers;
  }

  bool SpectrumDeconvolution::empty()
  {
    return logMzPeaks.empty();
  }

  std::vector<SpectrumDeconvolution::PeakGroup> &SpectrumDeconvolution::getPeakGroupsFromSpectrum(std::vector<std::vector<Size>> &prevMassBinVector,
                                                                                                  std::vector<double> &prevMinBinLogMassVector,
                                                                                                  FLASHDeconvHelperStructs::PrecalcularedAveragine &avg,
                                                                                                  unsigned int &msLevel)
  {
    auto minContinuousChargePeakCount =
        param.minContinuousChargePeakCount[msLevel - 1];//

    auto tmp = param.currentChargeRange - minContinuousChargePeakCount;
    tmp = tmp < 0 ? 0 : tmp;
    double massBinMaxValue = std::min(
        logMzPeaks[logMzPeaks.size() - 1].logMz -
        filter[tmp],
        log(param.currentMaxMass));
    auto binWidth = param.binWidth[msLevel - 1];
    tmp = minContinuousChargePeakCount - 1;
    tmp = tmp < 0 ? 0 : tmp;
    double massBinMinValue = logMzPeaks[0].logMz - filter[tmp];
    // std::cout<<massBinMinValue<<std::endl;
    double mzBinMinValue = logMzPeaks[0].logMz;
    double mzBinMaxValue = logMzPeaks[logMzPeaks.size() - 1].logMz;
    Size massBinNumber = getBinNumber(massBinMaxValue, massBinMinValue, binWidth) + 1;

    binOffsets = new long[param.currentChargeRange];

    for (int i = 0; i < param.currentChargeRange; i++)
    {
      binOffsets[i] = (long) round((mzBinMinValue - filter[i] - massBinMinValue) * binWidth);
    }

    hBinOffsets = new long *[param.hCharges.size()];
    for (Size k = 0; k < param.hCharges.size(); k++)
    {
      hBinOffsets[k] = new long[param.currentChargeRange];
      for (int i = 0; i < param.currentChargeRange; i++)
      {
        hBinOffsets[k][i] = (long) round((mzBinMinValue - harmonicFilter[k][i] - massBinMinValue) * binWidth);
      }
    }

    Size mzBinNumber = getBinNumber(mzBinMaxValue, mzBinMinValue, binWidth) + 1;
    auto *mzBinIntensities = new float[mzBinNumber];

    updateMzBins(mzBinMinValue, mzBinNumber, binWidth, mzBinIntensities);
    auto *massIntensities = new float[massBinNumber];

    massBins = boost::dynamic_bitset<>(massBinNumber);
    massBinsForThisSpectrum = boost::dynamic_bitset<>(massBinNumber);

    if (msLevel == 1)
    {
      unionPrevMassBins(massBinMinValue, prevMassBinVector, prevMinBinLogMassVector, msLevel);
    }

    auto perMassChargeRanges = updateMassBins(massBinMinValue, mzBinMinValue, massIntensities,
                                              mzBinIntensities, msLevel);

    getCandidatePeakGroups(mzBinMinValue, massBinMinValue,
                           massIntensities,
                           perMassChargeRanges, avg, msLevel);

    PeakGroupScoring scorer = PeakGroupScoring(peakGroups, param);
    peakGroups = scorer.scoreAndFilterPeakGroups(msLevel, avg);

    if (msLevel == 1)
    {
      //std::cout<<param.numOverlappedScans[msLevel - 1] << " " << prevMassBinVector.size() << std::endl;
      if (!prevMassBinVector.empty() && prevMassBinVector.size() >= (Size) param.numOverlappedScans[msLevel - 1])//
      {
        prevMassBinVector.erase(prevMassBinVector.begin());
        prevMinBinLogMassVector.erase(prevMinBinLogMassVector.begin());
      }

      std::vector<Size> mb;
      mb.reserve(peakGroups.size());
      for (auto &pg : peakGroups)//filteredPeakGroups
      {
        pg.peaks.shrink_to_fit();
        if (massBinsForThisSpectrum[pg.massBinIndex])
        {
          mb.push_back(pg.massBinIndex);
        }
      }

      prevMassBinVector.push_back(mb); //
      prevMinBinLogMassVector.push_back(massBinMinValue);
      prevMassBinVector.shrink_to_fit();
      prevMinBinLogMassVector.shrink_to_fit();
    }

    for (int i = 0; i < 3; i++)
    {
      delete[] perMassChargeRanges[i]; // delete array within matrix
    }// delete actual matrix
    delete[] perMassChargeRanges;
    delete[] mzBinIntensities;
    delete[] massIntensities;
    return peakGroups;
  }
}
