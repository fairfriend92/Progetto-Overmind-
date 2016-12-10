package com.example.myapplication;

import android.app.IntentService;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.provider.SyncStateContract;
import android.util.Log;

/**
 * Created by rodolfo on 07/12/16.
 */

public class Simulation extends IntentService{

    static {
        System.loadLibrary( "hello-world" );
    }

    /**
     * An IntentService must always have a constructor that calls the super constructor. The
     * string supplied to the super constructor is used to give a name to the IntentService's
     * background thread.
     */
    public Simulation() {
        super("Simulation");
    }

    /**
     * Method called by MainActivity on button press to shut down the service
     */
    static boolean shutdown = false;
    static public void shutDown () {
        shutdown = true;
    }

    static boolean[] presynapticSpikes = new boolean[Constants.NUMBER_OF_EXC_SYNAPSES];

    /**
     * Receive spikes broadcasted by the DataReceiver service
     */
    static public class MyReceiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            Log.d("MyReceiver", "Broadcast received " + action);
            if (action.equals("com.example.BROADCAST_SPIKES")) {
                presynapticSpikes = intent.getExtras().getBooleanArray("Presynaptic spikes");
            }
        }
    }

    @Override
    protected void onHandleIntent (Intent workIntent)
    {
        String macKernelVec4 = workIntent.getStringExtra("Kernel");
        // Create the object used to hold the information passed from one native call to the other
        long openCLObject = initializeOpenCL(macKernelVec4);
        if (openCLObject == -1) {
            Log.e("Simulation service", "Failed to initialize OpenCL");
            shutdown = true;
        }
        while (!shutdown) {
            openCLObject = simulateNetwork(presynapticSpikes, openCLObject);
        }
        if(closeOpenCL(openCLObject) == -1) { Log.e("Simulation service", "Failed to close OpenCL"); }
        shutdown = false;
        stopSelf();
    }

    /**
     * Java Native Interface
     */
    public native long simulateNetwork(boolean[] presynapticSpikes, long openCLObject);
    public native long initializeOpenCL(String macKernel);
    public native int closeOpenCL(long openCLObject);
}



