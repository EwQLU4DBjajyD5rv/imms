#ifndef __BEATKEEPER_H
#define __BEATKEEPER_H

#include <string>
#include <vector>

#include "analyzer.h"

#define MINBPM          50
#define MAXBPM          250

#define MINBEATLENGTH   (WINPERSEC*60/MAXBPM)
#define MAXBEATLENGTH   (WINPERSEC*60/MINBPM)
#define BEATSSIZE       (MAXBEATLENGTH-MINBEATLENGTH)

class BeatKeeper
{
public:
    BeatKeeper() { reset(); }
    void reset();
    void process(float power);

    void dump(const std::string &filename);
    const BeatKeeper &operator +=(const BeatKeeper &other);

protected:
    void process_window();

    long unsigned int samples;
    float average_with, *last_window, *current_window, *current_position;
    float data[2*MAXBEATLENGTH];
    float beats[BEATSSIZE];
};

class BeatManager
{
public:
    void process(const std::vector<double> &melfreqs);
    void finalize();
protected:
    BeatKeeper lofreq;
};

#endif
