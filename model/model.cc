#include "torch/ConnectedMachine.h"
#include "torch/DiskXFile.h"
#include "torch/Linear.h"
#include "torch/LogSoftMax.h"
#include "torch/MeanVarNorm.h"
#include "torch/MemoryDataSet.h"
#include "torch/OneHotClassFormat.h"
#include "torch/Tanh.h"

#include <iostream>
#include <vector>
#include <algorithm>

#include "model.h"
#include "song.h"
#include "immsutil.h"
#include "distance.h"

using namespace Torch;

using std::endl;
using std::cout;
using std::cerr;
using std::vector;

static const int num_inputs = NUM_FEATURES;
static const int num_hidden = NUM_FEATURES * 2;
static const int num_outputs = 2;

struct SimilarityModelPrivate
{
    SimilarityModelPrivate()
        : class_format(num_outputs),
          normalizer(0)
    {
        init_model();
    }

    OneHotClassFormat class_format;
    ConnectedMachine mlp;
    MeanVarNorm *normalizer;

    Allocator alloc;

    void init_model()
    {
        // *sigh* Torch is such garbage.
        MemoryDataSet feat_dat;
        normalizer = new(&alloc) MeanVarNorm(&feat_dat);

        Linear *c1 = new(&alloc) Linear(num_inputs, num_hidden);
        Tanh *c2 = new(&alloc) Tanh(num_hidden);
        Linear *c3 = new(&alloc) Linear(num_hidden, num_outputs);

        mlp.addFCL(c1);
        mlp.addFCL(c2);
        mlp.addFCL(c3);

        LogSoftMax *c4 = new(&alloc) LogSoftMax(num_outputs);
        mlp.addFCL(c4);

        mlp.build();
        mlp.setPartialBackprop();
    }
};

SimilarityModel::SimilarityModel() : impl(new SimilarityModelPrivate())
{
    DiskXFile::setLittleEndianMode();
    DiskXFile model_file(get_imms_root("model").c_str(), "r");
    impl->mlp.loadXFile(&model_file);
    DiskXFile norm_file(get_imms_root("norm").c_str(), "r");
    impl->normalizer->loadXFile(&norm_file);
    LOG(ERROR) << "model loading complete" << endl;
}

SimilarityModel::~SimilarityModel() {
}

float SimilarityModel::evaluate(float *features, bool normalize)
{
    // Garbage, I tell you!!
    Sequence feat_seq(0, NUM_FEATURES);
    feat_seq.addFrame(features);

    if (normalize)
        impl->normalizer->preProcessInputs(&feat_seq);

    impl->mlp.forward(&feat_seq);
    return (impl->mlp.outputs->frames[0][1]
            - impl->mlp.outputs->frames[0][0]) / 5;
}

float SimilarityModel::evaluate(const Song &s1, const Song &s2)
{
    vector<float> features;
    extract_features(s1, s2, &features);
    float feat_array[NUM_FEATURES];
    std::copy(features.begin(), features.end(), feat_array);
    return evaluate(feat_array);
}

static float find_max(float a[BEATSSIZE])
{
    return *std::max_element(a, a + BEATSSIZE);
}

static float find_min(float a[BEATSSIZE])
{
    return *std::min_element(a, a + BEATSSIZE);
}

static void mm_sums(const MixtureModel &mm, float sums[3])
{
    for (int i = 0; i < 3; ++i)
        sums[i] = 0;
    for (int i = 0; i < NUMGAUSS; ++i)
    {
        const Gaussian &g = mm.gauss[i];
        for (int j = 0; j < NUMCEPSTR; ++j)
            sums[j / 5] += g.weight * g.means[j];
    }
} 

bool SimilarityModel::extract_features(const Song &s1, const Song &s2,
        vector<float> *f)
{
    MixtureModel mm1, mm2;
    float b1[BEATSSIZE], b2[BEATSSIZE];
    float sums1[3], sums2[3];

    if (!s1.get_acoustic(&mm1, sizeof(MixtureModel), b1, sizeof(b1)))
        return false;
    if (!s2.get_acoustic(&mm2, sizeof(MixtureModel), b2, sizeof(b2)))
        return false;

    f->push_back(EMD::raw_distance(mm1, mm2));
    f->push_back(EMD::raw_distance(b1, b2));

    mm_sums(mm1, sums1);
    mm_sums(mm2, sums2);

    for (int j = 0; j < 3; ++j)
    {
        f->push_back(sums1[j]);
        f->push_back(sums2[j]);
    }

    f->push_back(find_max(b1));
    f->push_back(find_max(b2));

    f->push_back(find_min(b1));
    f->push_back(find_min(b2));

    return true;
}
