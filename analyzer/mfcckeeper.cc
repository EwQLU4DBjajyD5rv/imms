#include <torch/KMeans.h>
#include <torch/Random.h>
#include <torch/EMTrainer.h>
#include <torch/NLLMeasurer.h>
#include <torch/MemoryXFile.h>
#include <torch/Sequence.h>
#include <torch/MemoryDataSet.h>
#include <torch/DiagonalGMM.h>

#include <sqlite++.h>

#include "mfcckeeper.h"

#define NUMITER     100
#define ENDACCUR    0.0001

using namespace Torch;

class RandomSeeder {
public:
    RandomSeeder() { Torch::Random::seed(); }
};

Gaussian::Gaussian(float weight, float *means_, float *vars_) : weight(weight)
{
    memcpy(means, means_, sizeof(float) * NumDimensions);
    memcpy(vars, vars_, sizeof(float) * NumDimensions);
}

void MixtureModel::init(DiagonalGMM &gmm)
{
    for(int i = 0; i < NUMGAUSS; i++)
        gauss[i] = Gaussian(exp(gmm.log_weights[i]),
                gmm.means[i], gmm.var[i]);
}

SQLQuery &operator<<(SQLQuery &q, const MixtureModel &a)
{
    q.bind(&a.gauss, sizeof(a.gauss));
    return q;
}

struct MFCCKeeperPrivate
{
    MFCCKeeperPrivate()
            : cepseq(0, Gaussian::NumDimensions),
              cepseq_p(&cepseq) {
        cepdat.setInputs(&cepseq_p, 1);
    }
    Torch::Sequence cepseq;
    Torch::Sequence* cepseq_p;
    Torch::MemoryDataSet cepdat;
};

MFCCKeeper::MFCCKeeper() : impl(new MFCCKeeperPrivate), sample_number(0)
{
    static RandomSeeder seeder;
    memset(last_frame, 0, sizeof(last_frame));
    memset(last_delta, 0, sizeof(last_delta));
}

MFCCKeeper::~MFCCKeeper() {}

void diff_frames(float* dest, const float* f1, const float* f2) {
    for (int i = 0; i < NUMCEPSTR; ++i)
        dest[i] = f1[i] - f2[i];
}

void MFCCKeeper::process(float *cepstrum)
{
    ++sample_number;

    // Compute first and second order delta in addition to MFCCs.
    float delta[NUMCEPSTR];
    float meta_delta[NUMCEPSTR];

    diff_frames(delta, cepstrum, last_frame);
    diff_frames(meta_delta, delta, last_delta);

    static const int kFeatureSetSize = NUMCEPSTR * sizeof(float);
    memcpy(last_frame, cepstrum, kFeatureSetSize);
    memcpy(last_delta, delta, kFeatureSetSize);

    if (sample_number < 3)
        return;

    float buffer[Gaussian::NumDimensions];
    memcpy(buffer, cepstrum, kFeatureSetSize);
    memcpy(buffer + NUMCEPSTR, delta, kFeatureSetSize);
    memcpy(buffer + NUMCEPSTR * 2, meta_delta, kFeatureSetSize);

    impl->cepseq.addFrame(buffer, true);
}

void MFCCKeeper::finalize()
{
    KMeans kmeans(impl->cepdat.n_inputs, NUMGAUSS);
    kmeans.setROption("prior weights", 0.001);

    EMTrainer kmeans_trainer(&kmeans);
    kmeans_trainer.setIOption("max iter", NUMITER);
    kmeans_trainer.setROption("end accuracy", ENDACCUR);

    MeasurerList kmeans_measurers;
    MemoryXFile memfile1;
    NLLMeasurer nll_kmeans_measurer(kmeans.log_probabilities,
            &impl->cepdat, &memfile1);
    kmeans_measurers.addNode(&nll_kmeans_measurer);

    DiagonalGMM gmm(impl->cepdat.n_inputs, NUMGAUSS, &kmeans_trainer);
    gmm.setROption("prior weights", 0.001);
    gmm.setOOption("initial kmeans trainer measurers", &kmeans_measurers);

    // Measurers on the training dataset
    MeasurerList measurers;
    MemoryXFile memfile2;
    NLLMeasurer nll_meas(gmm.log_probabilities, &impl->cepdat, &memfile2);
    measurers.addNode(&nll_meas);

    EMTrainer trainer(&gmm);
    trainer.setIOption("max iter", NUMITER);
    trainer.setROption("end accuracy", ENDACCUR);

    trainer.train(&impl->cepdat, &measurers);

    result = gmm;

#if 0
    gmm.display();
#endif
}

const MixtureModel &MFCCKeeper::get_result()
{
    return result;
}
