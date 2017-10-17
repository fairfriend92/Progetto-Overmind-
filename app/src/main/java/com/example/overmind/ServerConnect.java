/**
 * Async task called to establish the tcp connection with the server to send the information
 * regarding the local neural network
 */

package com.example.overmind;

import android.content.Context;
import android.os.AsyncTask;
import android.util.Log;
import android.widget.Toast;

import java.io.IOException;
import java.io.ObjectOutputStream;
import java.net.Socket;
import java.util.ArrayList;

class ServerConnect extends AsyncTask<Context, Integer, SocketInfo> {

    private String SERVER_IP = MainActivity.serverIP;

    private Context context;
    private Terminal thisTerminal = new Terminal();
    private Socket clientSocket = null;


    protected SocketInfo doInBackground(Context ... contexts) {

        /**
         * Retrieve the global IP of this device
         */

        context = contexts[0];
        String ip = null;
        try (java.util.Scanner s = new java.util.Scanner(new java.net.URL("https://api.ipify.org").openStream(), "UTF-8").useDelimiter("\\A")) {
            ip = s.next();
        } catch (java.io.IOException e) {
            String stackTrace = Log.getStackTraceString(e);
            Log.e("ServerConnect", stackTrace);
        }
        thisTerminal.ip = ip;

        /**
         * Choose the number of neurons of the local network
         */

        // If the checkbox has been selected the application look up the appropriate number of
        // neurons for the device
        short numOfNeurons = 1;
        if (MainActivity.numOfNeuronsDeterminedByApp) {
            switch (MainActivity.renderer) {
                case "Mali-T720":
                    numOfNeurons = 56;
                    break;
                default:
                    // TODO default means could not identify GPU, exit app.
                    numOfNeurons = 1;
            }
        } else {

            // Else the number of neurons is chosen by the user and as such must be
            // retrieved from the text box

            numOfNeurons = Constants.NUMBER_OF_NEURONS;
        }

        thisTerminal.numOfNeurons = numOfNeurons;
        Constants.NUMBER_OF_NEURONS = numOfNeurons;
        short numOfSynapses = getNumOfSynapses(Constants.MAX_NUM_SYNAPSES);
        thisTerminal.numOfDendrites = numOfSynapses;
        thisTerminal.numOfSynapses = numOfSynapses;
        thisTerminal.natPort = 0;
        thisTerminal.serverIP = SERVER_IP;
        thisTerminal.presynapticTerminals = new ArrayList<>();
        thisTerminal.postsynapticTerminals = new ArrayList<>();

        /**
         * Establish connection with the Overmind and send terminal info
         */

        if (!MainActivity.serverConnectFailed) {

            try {
                clientSocket = new Socket(SERVER_IP, Constants.SERVER_PORT_TCP);
                clientSocket.setTrafficClass(Constants.IPTOS_RELIABILITY);
                clientSocket.setKeepAlive(true);
                //clientSocket.setTcpNoDelay(true);
            } catch (IOException e) {
                String stackTrace = Log.getStackTraceString(e);
                Log.e("ServerConnect", stackTrace);
                MainActivity.serverConnectFailed = true;
                MainActivity.serverConnectErrorNumber = 0;
            }

        }

        /**
         * If no error has occurred the info about the terminal are sent to the server
         */

        ObjectOutputStream output = null;

        if (clientSocket != null && !MainActivity.serverConnectFailed) {
            try {
                output = new ObjectOutputStream(clientSocket.getOutputStream());
                output.writeObject(thisTerminal);
                output.flush();
                publishProgress(0);
            } catch (IOException | NullPointerException e) {
                String stackTrace = Log.getStackTraceString(e);
                Log.e("ServerConnect", stackTrace);
            }
        }

        assert ip != null;
        assert output != null;
        return new SocketInfo(clientSocket, output, null);
    }

    protected void onProgressUpdate(Integer... progress) {
        int duration = Toast.LENGTH_SHORT;
        switch (progress[0]) {
            case 0:
                CharSequence text = "Connected with the Overmind server";
                Toast.makeText(context, text, duration).show();
        }
    }

    public native short getNumOfSynapses(short maxNumSynapses);

}