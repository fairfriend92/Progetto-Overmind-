#include "native_method.h"

#define potentialVar obj->neuronalDynVar[workId * 3]
#define recoveryVar obj->neuronalDynVar[workId * 3 + 1]
#define kahanCompensationVar obj->neuronalDynVar[workId * 3 + 2]

#define aPar simulationParameters[0]
#define bPar simulationParameters[1]
#define cPar simulationParameters[2]
#define dPar simulationParameters[3]
#define IPar simulationParameters[4]

// Maximum number of multiplications needed to compute the current response of a synapse
 int maxNumberMultiplications = (int) (SYNAPSE_FILTER_ORDER * SAMPLING_RATE / ABSOLUTE_REFRACTORY_PERIOD);
// Size of the buffers needed to store data
size_t synapseCoeffBufferSize = SYNAPSE_FILTER_ORDER * 2 * sizeof(cl_float);
size_t synapseInputBufferSize =  maxNumberMultiplications * (NUMBER_OF_EXC_SYNAPSES + NUMBER_OF_INH_SYNAPSES) * sizeof(cl_char);

extern "C" jshort Java_com_example_overmind_ServerConnect_getNumOfSynapses (
        JNIEnv *env, jobject  thiz) {

    struct OpenCLObject *obj;
    obj = (struct OpenCLObject *)malloc(sizeof(struct OpenCLObject));

    if (!createContext(&obj->context))
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Failed to create an OpenCL context");
    }

    if (!createCommandQueue(obj->context, &obj->commandQueue, &obj->device))
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Failed to create an OpenCL command queue");
    }

    size_t maxWorkGroupSize;

    clGetDeviceInfo(obj->device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT, sizeof(cl_uint), &obj->intVectorWidth, NULL);
    LOGD("Device info: Preferred vector width for integers: %d ", obj->intVectorWidth);

    clGetDeviceInfo(obj->device, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(size_t), &maxWorkGroupSize, NULL);
    LOGD("Device info: Maximum work group size: %d ", (int)maxWorkGroupSize);

    obj->maxWorkGroupSize = maxWorkGroupSize < (1024 / obj->intVectorWidth) ? maxWorkGroupSize : (1024 / obj->intVectorWidth);

    return (short) (4 * obj->maxWorkGroupSize);

}

extern "C" jlong Java_com_example_overmind_SimulationService_initializeOpenCL (
        JNIEnv *env, jobject thiz, jstring jKernel, jshort jNumOfNeurons) {

    size_t synapseWeightsBufferSize = (NUMBER_OF_EXC_SYNAPSES + NUMBER_OF_INH_SYNAPSES) * jNumOfNeurons * sizeof(cl_float);
    size_t currentBufferSize = jNumOfNeurons * sizeof(cl_int);

    struct OpenCLObject *obj;
    obj = (struct OpenCLObject *)malloc(sizeof(struct OpenCLObject));

    const char *kernelString = env->GetStringUTFChars(jKernel, JNI_FALSE);

    // The context must be created anew since the first time getNumOfSynapses didn't pass it back
    // to the java side of the application
    if (!createContext(&obj->context))
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Failed to create an OpenCL context");
    }

    if (!createCommandQueue(obj->context, &obj->commandQueue, &obj->device))
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Failed to create an OpenCL command queue");
    }

    if (!createProgram(obj->context, obj->device, kernelString, &obj->program))
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Failed to create OpenCL program");
    }

    /**
     * Query the device to find various information. These are merely indicative since we must account for the
     * complexity of the Kernel. For more information look at clGetKernelWorkGroupInfo in the simulateDynamics method.
     */

    size_t maxWorkItems[3];
    size_t maxWorkGroupSize;
    cl_uint maxWorkItemDimension;
    cl_uint maxComputeUnits;
    cl_bool compilerAvailable;
    cl_uint addressBits;
    char deviceName[256];

    clGetDeviceInfo(obj->device, CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT, sizeof(cl_uint), &obj->intVectorWidth, NULL);

    clGetDeviceInfo(obj->device, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(size_t), &maxWorkGroupSize, NULL);

    obj->maxWorkGroupSize = maxWorkGroupSize < (1024 / obj->intVectorWidth) ? maxWorkGroupSize : (1024 / obj->intVectorWidth);

    clGetDeviceInfo(obj->device, CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, sizeof(cl_uint), &maxWorkItemDimension, NULL);
    LOGD("Device info: Maximum work item dimension: %d", maxWorkItemDimension);

    clGetDeviceInfo(obj->device, CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(size_t[3]), maxWorkItems, NULL);
    LOGD("Device info: Maximum work item sizes for each dimension: %d, %d, %d", (int)maxWorkItems[0], (int)maxWorkItems[1], (int)maxWorkItems[2]);

    clGetDeviceInfo(obj->device, CL_DEVICE_NAME, sizeof(char[256]), deviceName, NULL);
    LOGD("Device info: Device name: %s", deviceName);

    clGetDeviceInfo(obj->device, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cl_uint), &maxComputeUnits, NULL);
    LOGD("Device info: Maximum compute units: %d", maxComputeUnits);

    clGetDeviceInfo(obj->device, CL_DEVICE_COMPILER_AVAILABLE, sizeof(cl_bool), &compilerAvailable, NULL);
    LOGD("Device info: Device compiler available: %d", compilerAvailable);

    clGetDeviceInfo(obj->device, CL_DEVICE_ADDRESS_BITS, sizeof(cl_uint), &addressBits, NULL);
    LOGD("Device info: Device address bits: %d", addressBits);

    obj->kernel = clCreateKernel(obj->program, "simulate_dynamics", &obj->errorNumber);
    if (!checkSuccess(obj->errorNumber))
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Failed to create OpenCL kernel");
    }

    // Release the string containing the kernel since it has been passed already to createProgram
    env->ReleaseStringUTFChars(jKernel, kernelString);

    bool createMemoryObjectsSuccess = true;
    obj->numberOfMemoryObjects= 4;

    // Allocate memory from the host
    float *neuronalDynVar = new float[3 * jNumOfNeurons];
    obj->neuronalDynVar = neuronalDynVar;

    // Ask the OpenCL implementation to allocate buffers to pass data to and from the kernels
    obj->memoryObjects[0] = clCreateBuffer(obj->context, CL_MEM_READ_ONLY| CL_MEM_ALLOC_HOST_PTR, synapseCoeffBufferSize, NULL, &obj->errorNumber);
    createMemoryObjectsSuccess &= checkSuccess(obj->errorNumber);

    obj->memoryObjects[1] = clCreateBuffer(obj->context, CL_MEM_READ_ONLY | CL_MEM_ALLOC_HOST_PTR, synapseWeightsBufferSize, NULL, &obj->errorNumber);
    createMemoryObjectsSuccess &= checkSuccess(obj->errorNumber);

    obj->memoryObjects[2] = clCreateBuffer(obj->context, CL_MEM_READ_ONLY | CL_MEM_ALLOC_HOST_PTR, synapseInputBufferSize, NULL, &obj->errorNumber);
    createMemoryObjectsSuccess &= checkSuccess(obj->errorNumber);

    obj->memoryObjects[3] = clCreateBuffer(obj->context, CL_MEM_READ_WRITE| CL_MEM_ALLOC_HOST_PTR, currentBufferSize, NULL, &obj->errorNumber);
    createMemoryObjectsSuccess &= checkSuccess(obj->errorNumber);

    if (!createMemoryObjectsSuccess)
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Failed to create OpenCL buffer");
    }

    // Map the memory buffers created by the OpenCL implementation so we can access them on the CPU
    bool mapMemoryObjectsSuccess = true;

    obj->synapseCoeff = (cl_float*)clEnqueueMapBuffer(obj->commandQueue, obj->memoryObjects[0], CL_TRUE, CL_MAP_WRITE, 0, synapseCoeffBufferSize, 0, NULL, NULL, &obj->errorNumber);
    mapMemoryObjectsSuccess &= checkSuccess(obj->errorNumber);

    obj->synapseWeights = (cl_float *)clEnqueueMapBuffer(obj->commandQueue, obj->memoryObjects[1], CL_TRUE, CL_MAP_WRITE, 0, synapseWeightsBufferSize, 0, NULL, NULL, &obj->errorNumber);
    mapMemoryObjectsSuccess &= checkSuccess(obj->errorNumber);

    obj->current = (cl_int *)clEnqueueMapBuffer(obj->commandQueue, obj->memoryObjects[3], CL_TRUE, CL_MAP_WRITE, 0, currentBufferSize, 0, NULL, NULL, &obj->errorNumber);
    mapMemoryObjectsSuccess &= checkSuccess(obj->errorNumber);

    if (!mapMemoryObjectsSuccess)
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Failed to map buffer");
    }

    // Initialize the coefficients array by sampling the exponential kernel of the synapse filter
    float tExc = (float) (SAMPLING_RATE / EXC_SYNAPSE_TIME_SCALE);
    float tInh = (float) (SAMPLING_RATE / INH_SYNAPSE_TIME_SCALE);
    // Extend the loop beyond the synapse filter order to account for possible overflows of the filter input
    for (int index = 0; index < SYNAPSE_FILTER_ORDER * 2; index++)
    {
        obj->synapseCoeff[index] = index%2 == 0 ? (cl_float )index * tExc * expf( - index * tExc) : (cl_float)(-index * tInh * expf( - index * tInh));
        //LOGD("The synapse coefficients are: \tcoefficient %d \tvalue %f", index, (float) (obj->synapseCoeff[index]));
    }

    // For testing purposes we initialize the synapses weights here
    for (int index = 0; index < (NUMBER_OF_EXC_SYNAPSES + NUMBER_OF_INH_SYNAPSES) * jNumOfNeurons; index++)
    {
        obj->synapseWeights[index] = index%2 == 0 ? 100.0f : 33.0f;
    }

    // The following could be local kernel variables as well, but that would call for a costly thread
    // syncrhonization. Instead we initialize them outside the kernel as global variables, since additional
    // memory access times are negligible due to the embedded nature of the device
    for (int index = 0; index < jNumOfNeurons; index++)
    {
        obj->neuronalDynVar[3 * index] = -65.0f;
        obj->neuronalDynVar[3 * index + 1] = -13.0f;
        obj->neuronalDynVar[3 * index + 2] = 0.0f;
        obj->current[index] = (cl_int)0;
    }

    // Un-map the pointers used to initialize the memory buffers
    if (!checkSuccess(clEnqueueUnmapMemObject(obj->commandQueue, obj->memoryObjects[0], obj->synapseCoeff, 0, NULL, NULL)))
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Unmap memory objects failed");
    }

    if (!checkSuccess(clEnqueueUnmapMemObject(obj->commandQueue, obj->memoryObjects[1], obj->synapseWeights, 0, NULL, NULL)))
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Unmap memory objects failed");
    }

    if (!checkSuccess(clEnqueueUnmapMemObject(obj->commandQueue, obj->memoryObjects[3], obj->current, 0, NULL, NULL)))
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Unmap memory objects failed");
    }

    return (long) obj;
}

extern "C" jbyteArray Java_com_example_overmind_SimulationService_simulateDynamics(
        JNIEnv *env, jobject thiz, jcharArray jSynapseInput, jlong jOpenCLObject, jshort jNumOfNeurons, jfloatArray jSimulationParameters) {

    struct OpenCLObject *obj;
    obj = (struct OpenCLObject *)jOpenCLObject;

    size_t synapseWeightsBufferSize = (NUMBER_OF_EXC_SYNAPSES + NUMBER_OF_INH_SYNAPSES)* jNumOfNeurons * sizeof(cl_float);

    size_t currentBufferSize = jNumOfNeurons * sizeof(cl_int);
    size_t neuronalDynVarBufferSize = jNumOfNeurons * 3 * sizeof(cl_float);
    char dataBytes = (jNumOfNeurons % 8) == 0 ? (char)(jNumOfNeurons / 8) : (char)(jNumOfNeurons / 8 + 1);
    size_t actionPotentialsBufferSize = dataBytes * sizeof(cl_uchar);


    jchar *synapseInput = env->GetCharArrayElements(jSynapseInput, JNI_FALSE);
    jfloat *simulationParameters = env->GetFloatArrayElements(jSimulationParameters, JNI_FALSE);

    /* [Input initialization] */
    /**
     * Map the memory buffer that holds the input of the synapses, initialize it with the data received
     * through the Java Native Interface and then un-map it
     */
    bool mapMemoryObjectsSuccess = true;
    // Map the buffer
    obj->synapseInput = (cl_char*)clEnqueueMapBuffer(obj->commandQueue, obj->memoryObjects[2], CL_TRUE, CL_MAP_WRITE, 0, synapseInputBufferSize, 0, NULL, NULL, &obj->errorNumber);
    mapMemoryObjectsSuccess &= checkSuccess(obj->errorNumber);

    // Catch eventual errors
    if (!mapMemoryObjectsSuccess)
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Failed to map buffer");
    }

    // Initialize the buffer on the CPU side with the data received from Java Native Interface
    for (int index = 0; index < maxNumberMultiplications * (NUMBER_OF_EXC_SYNAPSES + NUMBER_OF_INH_SYNAPSES); index++)
    {
        obj->synapseInput[index] = (cl_char)synapseInput[index];
    }

    // Un-map the buffer
    if (!checkSuccess(clEnqueueUnmapMemObject(obj->commandQueue, obj->memoryObjects[2], obj->synapseInput, 0, NULL, NULL)))
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Unmap memory objects failed");
    }

    // Release the java array since the data has been passed to the memory buffer
    env->ReleaseCharArrayElements(jSynapseInput, synapseInput, 0);

    /* [Input initialization] */

    /* [Set Kernel Arguments] */
    // Tell the kernels which data to use before they are scheduled
    bool setKernelArgumentSuccess = true;
    setKernelArgumentSuccess &= checkSuccess(clSetKernelArg(obj->kernel, 0, sizeof(cl_mem), &obj->memoryObjects[0]));
    setKernelArgumentSuccess &= checkSuccess(clSetKernelArg(obj->kernel, 1, sizeof(cl_mem), &obj->memoryObjects[1]));
    setKernelArgumentSuccess &= checkSuccess(clSetKernelArg(obj->kernel, 2, sizeof(cl_mem), &obj->memoryObjects[2]));
    setKernelArgumentSuccess &= checkSuccess(clSetKernelArg(obj->kernel, 3, sizeof(cl_mem), &obj->memoryObjects[3]));

    // Catch eventual errors
    if (!setKernelArgumentSuccess)
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Failed to set OpenCL kernel arguments");
    }
    /* [Set Kernel Arguments] */

    /* [Kernel execution] */
    // Uncomment for more information on the kernel

    /*
    size_t compileWorkGroupSize[3];
    cl_ulong localMemorySize;
    cl_ulong privateMemorySize;
    */

    // Get the maximum number of work items allowed based on scheduled kernel
    size_t kernelWorkGroupSize;
    clGetKernelWorkGroupInfo(obj->kernel, obj->device, CL_KERNEL_WORK_GROUP_SIZE, sizeof(size_t), &kernelWorkGroupSize, NULL);

    /*
    LOGD("Kernel info: maximum work group size: %d", kernelWorkGroupSize);
    clGetKernelWorkGroupInfo(obj->kernel, obj->device, CL_KERNEL_COMPILE_WORK_GROUP_SIZE, sizeof(size_t[3]), &compileWorkGroupSize, NULL);
    LOGD("Kernel info: compile work group size: %d %d %d", compileWorkGroupSize[0], compileWorkGroupSize[1], compileWorkGroupSize[2]);
    clGetKernelWorkGroupInfo(obj->kernel, obj->device, CL_KERNEL_LOCAL_MEM_SIZE, sizeof(cl_ulong), &localMemorySize, NULL);
    LOGD("Kernel info: local memory size in B: %lu", localMemorySize);
    clGetKernelWorkGroupInfo(obj->kernel, obj->device, CL_KERNEL_PRIVATE_MEM_SIZE, sizeof(cl_ulong), &privateMemorySize, NULL);
    LOGD("Kernel info: private memory size in B: %lu", privateMemorySize);
     */

    // Number of kernel instances
    size_t localWorksize[1] = {kernelWorkGroupSize};
    size_t globalWorksize[1] = {localWorksize[0] * jNumOfNeurons};

    bool openCLFailed = false;

    // Exit with error if # workitems allowed is different from the theoretic one provided
    // by the device
    if (kernelWorkGroupSize != obj->maxWorkGroupSize) { openCLFailed = true; }

    // Uncomment the following for profiling info
    /*
    //  An event to associate with the kernel. Allows us to to retrieve profiling information later
    cl_event event = 0;

    // Enqueue the kernel
    if (!checkSuccess(clEnqueueNDRangeKernel(obj->commandQueue, obj->kernel, 1, NULL, globalWorksize, localWorksize, 0, NULL, &event)))
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Failed to enqueue OpenCL kernel");
        openCLFailed = true;
    }
    */

    // Comment out this clEnqueueNDRangeKernel when profiling

    // Enqueue the kernel
    if (!checkSuccess(clEnqueueNDRangeKernel(obj->commandQueue, obj->kernel, 1, NULL, globalWorksize, localWorksize, 0, NULL, NULL)))
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Failed to enqueue OpenCL kernel");
        openCLFailed = true;
    }

    // Wait for kernel execution completion
    if (!checkSuccess(clFinish(obj->commandQueue)))
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Failed waiting for kernel execution to finish");
        openCLFailed = true;
    }

    // Print the profiling information for the event
    /*
    if(!printProfilingInfo(event))
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Failed to print profiling info");
        openCLFailed = true;
    }

    // Release the event object
    if (!checkSuccess(clReleaseEvent(event)))
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Failed releasing the event");
        openCLFailed = true;
    }
    */

    // If an error occurred return an empty byte array
    if (openCLFailed) {
        jbyteArray errorByte = env->NewByteArray(0);
        return errorByte;
    }
    /* [Kernel execution] */

    /* [Simulate the neuronal dynamics] */

    // Map the OpenCL memory buffer so that it can be accessed by the host
    obj->current = (cl_int *)clEnqueueMapBuffer(obj->commandQueue, obj->memoryObjects[3], CL_TRUE, CL_MEM_READ_WRITE, 0, currentBufferSize, 0, NULL, NULL, &obj->errorNumber);
    mapMemoryObjectsSuccess &= checkSuccess(obj->errorNumber);

    if (!mapMemoryObjectsSuccess)
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Failed to map buffer");
    }

    char actionPotentials[dataBytes];

    //LOGD("Inizio");

    // Simulate the neuronal dynamics using the current computer by the OpenCL implementation
    for (short workId = 0; workId < jNumOfNeurons; workId++)
    {
        // Convert back from int to float
        float unsignedCurrentFloat = (float)(obj->current[workId]) / 32768000.0f;
        float currentFloat = obj->current[workId] > 0 ? unsignedCurrentFloat : (-unsignedCurrentFloat);

        // Compute the potential and the recovery variable using Euler integration and Izhikevich model
        potentialVar += 0.04f * pow(potentialVar, 2) + 5.0f * potentialVar + 140.0f - recoveryVar + currentFloat + IPar;
        recoveryVar += aPar * (bPar * potentialVar - recoveryVar);

        // Uncomment the following to use inverse Euler for the recovery variable
        /*
        recoveryVar = (0.5f * 0.02f * 0.2f * potentialVar + recoveryVar) / (1.0f + 0.5f * 0.02f);
        recoveryVar += 0.5f * 0.02f * (0.2f * potentialVar - recoveryVar);
        */

        // Uncomment the following to use Kahan compensation on the recovery variable
        /*
        float y = aPar * (bPar * potentialVar - recoveryVar) - kahanCompensationVar;
        float t = recoveryVar + y;
        kahanCompensationVar = (t - recoveryVar) - y;
        recoveryVar = t;
        */

        // Set the bits corresponding to the neurons that have fired
        if (potentialVar >= 30.0f)
        {
            //LOGD("Set %f %f %f %d", potentialVar, recoveryVar, currentFloat, workId);
            actionPotentials[(short)(workId / 8)] |= (1 << (workId - (short)(workId / 8) * 8));
            recoveryVar += dPar;
            potentialVar = cPar;
        }
        else
        {
            //LOGD("Unset %f %f %f %d", potentialVar, recoveryVar, currentFloat, workId);
            actionPotentials[(short)(workId / 8)] &= ~(1 << (workId - (short)(workId / 8) * 8));
        }

        obj->current[workId] = (cl_long)0;
    }
    /* [Simulate the neuronal dynamics] */

    //LOGD("Fine");

    if (!checkSuccess(clEnqueueUnmapMemObject(obj->commandQueue, obj->memoryObjects[3], obj->current, 0, NULL, NULL)))
    {
        cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects);
        LOGE("Unmap memory objects failed");
    }

    // Release the array storing the simulation parameters
    env->ReleaseFloatArrayElements(jSimulationParameters, simulationParameters, 0);

    // Create the array where to store the output
    jbyteArray outputSpikes = env->NewByteArray(dataBytes);
    // Copy the content in the buffer to the java array, using the pointer created by the OpenCL implementation
    env->SetByteArrayRegion(outputSpikes, 0, dataBytes, reinterpret_cast<jbyte*>(&actionPotentials));

    return outputSpikes;

}

extern "C" void Java_com_example_overmind_SimulationService_closeOpenCL(
        JNIEnv *env, jobject thiz,  jlong jOpenCLObject) {

    struct OpenCLObject *obj;
    obj = (struct OpenCLObject *) jOpenCLObject;

    if (!cleanUpOpenCL(obj->context, obj->commandQueue, obj->program, obj->kernel, obj->memoryObjects, obj->numberOfMemoryObjects))
    {
        LOGE("Failed to clean-up OpenCL");
    };

    // Delete the memory allocated by the host
    delete obj->neuronalDynVar;

    free(obj);

}