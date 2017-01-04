#define SHIFT_FACTOR 15

#define potential neuronalDynVar[workId * 2]
#define recovery neuronalDynVar[workId * 2 + 1]
#define current localData[workId]
#define counter localData[workId]

#pragma OPENCL EXTENSION cl_khr_fp16 : enable

int synaptic_current (uchar localId, ushort workId, uchar16 index,
		      __constant half* weights, __constant float* coeff)
{
  float4 coeffVec = (float4)(coeff[index.s0] + coeff[index.s1] + coeff[index.s2] + coeff[index.s3],
			     coeff[index.s4] + coeff[index.s5] + coeff[index.s6] + coeff[index.s7],
			     coeff[index.s8] + coeff[index.s9] + coeff[index.sa] + coeff[index.sb],
			     coeff[index.sc] + coeff[index.sd] + coeff[index.se] + coeff[index.sf]);

  half4 weightsVec = (half4)(weights[workId * 1024 + localId * 4],
			     weights[workId * 1024 + localId * 4 + 1],
			     weights[workId * 1024 + localId * 4 + 2],
			     weights[workId * 1024 + localId * 4 + 3]);

  return (int)(dot(coeffVec, convert_float4(weightsVec)) * pown(2.0f, SHIFT_FACTOR));
}
  
__kernel void synapse_vec4(__constant float* coeff, __constant half* weights,
			   __constant uchar* input, __global int* localData,
			   __global float* neuronalDynVar, __global uchar* actionPotentials)
{  
  uchar localId = get_local_id(0);
  ushort workId = (ushort)(get_global_id(0) / get_local_size(0));

  int increment = synaptic_current(localId, workId, vload16(localId, input), weights, coeff);
  
  atomic_add(&current, increment);
  
  atomic_inc(&counter);

  mem_fence(CLK_LOCAL_MEM_FENCE);
  
  if (counter == get_local_size(0) - 1)
    {
      float oldPotential = potential;
      float oldRecovery = recovery;
      
      potential = 0.04f * pown(oldPotential, 2)  + 5 * oldPotential + 140 - oldRecovery + (float)(current << SHIFT_FACTOR);
      recovery = 0.02f * (0.2f * oldPotential - oldRecovery);
      /*
      if (potential >= 15.0f)
	{
	  actionPotentials[(ushort)(workId / 8)] |= 1 << workId - (ushort)(workId / 8) * 8;
	  recovery = recovery + 6.0f;
	  potential = -65.0f;
	}
      else
	{
	  actionPotentials[(ushort)(workId / 8)] &= ~(1 << workId - (ushort)(workId / 8) * 8);
	}
      */

      actionPotentials[0] ^= 1 << 1; 
      
      current = counter = 0;
    }
  
}
    
  

