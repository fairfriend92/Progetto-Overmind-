//
// Created by rodolfo on 08/12/16.
//

#ifndef MYAPPLICATION_NATIVE_METHOD_H_H
#define MYAPPLICATION_NATIVE_METHOD_H_H

#endif //MYAPPLICATION_NATIVE_METHOD_H_H

// Constants
#define SAMPLING_RATE 0.5
#define EXC_SYNAPSE_TIME_SCALE 1
#define INH_SYNAPSE_TIME_SCALE 3
#define ABSOLUTE_REFRACTORY_PERIOD 2
#define MIN_WEIGHT 0.0078f
#define MEAN_RATE_INCREMENT 0.01f
#define SYNAPSE_FILTER_ORDER 16

#include <android/log.h>
#include <jni.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "CL/cl.h"
#include "common.h"

struct OpenCLObject {
    // OpenCL implementation
    cl_context context = 0;
    cl_command_queue commandQueue = 0;
    cl_program program = 0;
    cl_device_id device = 0;
    cl_kernel kernel = 0;
    cl_int errorNumber = 0;
    int numberOfMemoryObjects = 10;
    cl_mem memoryObjects[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    cl_uint floatVectorWidth;
    size_t maxWorkGroupSize;

    // Pointers to the memory buffers
    cl_float *synapseCoeff;
    cl_float *synapseWeights;
    cl_uchar *synapseInput;
    cl_ushort *synapseIndexes;
    cl_int *current;
    cl_ushort *neuronsIndexes;
    cl_float *presynFiringRates;
    cl_float *postsynFiringRates;
    cl_float *updateWeightsFlags;
    cl_int *globalIdOffset;
    float *neuronalDynVar;
};

