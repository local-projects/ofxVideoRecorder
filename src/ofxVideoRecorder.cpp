//
//  ofxVideoRecorder.cpp
//  ofxVideoRecorderExample
//
//  Created by Timothy Scaffidi on 9/23/12.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//

#include "ofxVideoRecorder.h"
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
#include <unistd.h>
#elif defined(TARGET_WIN32)
#include <io.h>
#endif
#include <fcntl.h>

//--------------------------------------------------------------
//--------------------------------------------------------------
int setNonBlocking(int fd){
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
    int flags;

    /* If they have O_NONBLOCK, use the Posix way to do it */
#if defined(O_NONBLOCK)
    /* Fixme: O_NONBLOCK is defined but broken on SunOS 4.1.x and AIX 3.2.5. */
    if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
        flags = 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    /* Otherwise, use the old way of doing it */
    flags = 1;
    return ioctl(fd, FIOBIO, &flags);
#endif
#endif
	return 0;
}

//--------------------------------------------------------------
//--------------------------------------------------------------
execThread::execThread(){
    execCommand = "";
    initialized = false;
}

//--------------------------------------------------------------
void execThread::setup(string command){
    execCommand = command;
    initialized = false;
    startThread(true);
}

//--------------------------------------------------------------
void execThread::threadedFunction(){
    if(isThreadRunning()){
        ofLogVerbose("execThread") << "starting command: " <<  execCommand;
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
		int result = system(execCommand.c_str());
#elif defined(TARGET_WIN32)
		//string cmd = "start " + execCommand;
		initialized = true;
		//int result = system(cmd.c_str());
		int result = system(execCommand.c_str());
#endif
        if (result == 0) {
            ofLogVerbose("execThread") << "command completed successfully.";
            initialized = true;
        } else {
            ofLogError("execThread") << "command failed with result: " << result;
        }
    }
}

//--------------------------------------------------------------
//--------------------------------------------------------------
ofxVideoDataWriterThread::ofxVideoDataWriterThread(){
}

//--------------------------------------------------------------
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
void ofxVideoDataWriterThread::setup(string filePath, lockFreeQueue<ofPixels *> * q) {
	this->filePath = filePath;
	fd = -1;
#elif defined(TARGET_WIN32)
void ofxVideoDataWriterThread::setup(HANDLE videoHandle_, lockFreeQueue<ofPixels *> * q) {
	videoHandle = videoHandle_;
#endif
	queue = q;
	bIsWriting = false;
	bClose = false;
	bNotifyError = false;
	startThread(true);
}

//--------------------------------------------------------------
void ofxVideoDataWriterThread::threadedFunction(){
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
    if(fd == -1){
        ofLogVerbose("ofxVideoDataWriterThread") << "opening pipe: " <<  filePath;
        fd = ::open(filePath.c_str(), O_WRONLY);
        ofLogWarning("ofxVideoDataWriterThread") << "got file descriptor " << fd;
    }
#endif

    while(isThreadRunning())
    {
        ofPixels * frame = NULL;
        if(queue->Consume(frame) && frame){
            bIsWriting = true;
            int b_offset = 0;
            int b_remaining = frame->getWidth()*frame->getHeight()*frame->getBytesPerPixel();

            while(b_remaining > 0 && isThreadRunning())
            {
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
				errno = 0;

				int b_written = ::write(fd, ((char *)frame->getData()) + b_offset, b_remaining);
#elif defined(TARGET_WIN32)
				DWORD b_written;
				if (!WriteFile(videoHandle, ((char *)frame->getPixels()) + b_offset, b_remaining, &b_written, 0)) {
					LPTSTR errorText = NULL;

					FormatMessageW(
						// use system message tables to retrieve error text
						FORMAT_MESSAGE_FROM_SYSTEM
						// allocate buffer on local heap for error text
						| FORMAT_MESSAGE_ALLOCATE_BUFFER
						// Important! will fail otherwise, since we're not 
						// (and CANNOT) pass insertion parameters
						| FORMAT_MESSAGE_IGNORE_INSERTS,
						NULL,    // unused with FORMAT_MESSAGE_FROM_SYSTEM
						GetLastError(),
						MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
						(LPTSTR)&errorText,  // output 
						0, // minimum size for output buffer
						NULL);   // arguments - see note 
					wstring ws = errorText;
					string error(ws.begin(), ws.end());
					ofLogNotice("Video Thread") << "WriteFile to pipe failed: " << error;
					break;
				}
#endif
                if(b_written > 0){
                    b_remaining -= b_written;
                    b_offset += b_written;
                    if (b_remaining != 0) {
                        ofLogWarning("ofxVideoDataWriterThread") << ofGetTimestampString("%H:%M:%S:%i") << " - b_remaining is not 0 -> " << b_written << " - " << b_remaining << " - " << b_offset << ".";
                        // break;
                    }
                }
                else if (b_written < 0) {
                    ofLogError("ofxVideoDataWriterThread") << ofGetTimestampString("%H:%M:%S:%i") << " - write to PIPE failed with error -> " << errno << " - " << strerror(errno) << ".";
                    bNotifyError = true;
                    break;
                }
                else {
                    if(bClose){
                        ofLogVerbose("ofxVideoDataWriterThread") << ofGetTimestampString("%H:%M:%S:%i") << " - Nothing was written and bClose is TRUE.";
                        break; // quit writing so we can close the file
                    }
                    ofLogWarning("ofxVideoDataWriterThread") << ofGetTimestampString("%H:%M:%S:%i") << " - Nothing was written. Is this normal?";
                }

                if (!isThreadRunning()) {
                    ofLogWarning("ofxVideoDataWriterThread") << ofGetTimestampString("%H:%M:%S:%i") << " - The thread is not running anymore let's get out of here!";
                }
            }
            bIsWriting = false;
            frame->clear();
            delete frame;
        }
        else{
			conditionMutex.lock();
			condition.wait(conditionMutex);
			conditionMutex.unlock();
        }
    }

    ofLogVerbose("ofxVideoDataWriterThread") << "closing pipe: " <<  filePath;
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
	::close(fd);
#elif defined(TARGET_WIN32)
	FlushFileBuffers(videoHandle);
	DisconnectNamedPipe(videoHandle);
	CloseHandle(videoHandle);
#endif
}

//--------------------------------------------------------------
void ofxVideoDataWriterThread::signal(){
    condition.signal();
}

//--------------------------------------------------------------
void ofxVideoDataWriterThread::setPipeNonBlocking(){
    setNonBlocking(fd);
}

//--------------------------------------------------------------
//--------------------------------------------------------------
ofxAudioDataWriterThread::ofxAudioDataWriterThread(){
}

//--------------------------------------------------------------
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
void ofxAudioDataWriterThread::setup(string filePath, lockFreeQueue<audioFrameShort *> *q) {
	this->filePath = filePath;
	fd = -1;
#elif defined(TARGET_WIN32)
void ofxAudioDataWriterThread::setup(HANDLE audioHandle_, lockFreeQueue<audioFrameShort *> *q) {
	audioHandle = audioHandle_;
#endif
	queue = q;
	bIsWriting = false;
	bNotifyError = false;
	startThread(true);
}


//--------------------------------------------------------------
void ofxAudioDataWriterThread::threadedFunction(){
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
    if(fd == -1){
        ofLogVerbose("ofxAudioDataWriterThread") << "opening pipe: " <<  filePath;
        fd = ::open(filePath.c_str(), O_WRONLY);
        ofLogWarning("ofxVideoDataWriterThread") << "got file descriptor " << fd;
    }
#endif

    while(isThreadRunning())
    {
        audioFrameShort * frame = NULL;
        if(queue->Consume(frame) && frame){
            bIsWriting = true;
            int b_offset = 0;
            int b_remaining = frame->size*sizeof(short);
            while(b_remaining > 0 && isThreadRunning()){
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
				int b_written = ::write(fd, ((char *)frame->data) + b_offset, b_remaining);
#elif defined(TARGET_WIN32)
				DWORD b_written;
				if (!WriteFile(audioHandle, ((char *)frame->data) + b_offset, b_remaining, &b_written, 0)) {
					LPTSTR errorText = NULL;

					FormatMessageW(
						// use system message tables to retrieve error text
						FORMAT_MESSAGE_FROM_SYSTEM
						// allocate buffer on local heap for error text
						| FORMAT_MESSAGE_ALLOCATE_BUFFER
						// Important! will fail otherwise, since we're not 
						// (and CANNOT) pass insertion parameters
						| FORMAT_MESSAGE_IGNORE_INSERTS,
						NULL,    // unused with FORMAT_MESSAGE_FROM_SYSTEM
						GetLastError(),
						MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
						(LPTSTR)&errorText,  // output 
						0, // minimum size for output buffer
						NULL);   // arguments - see note 
					wstring ws = errorText;
					string error(ws.begin(), ws.end());
					ofLogNotice("Audio Thread") << "WriteFile to pipe failed: " << error;
				}
#endif
                if(b_written > 0){
                    b_remaining -= b_written;
                    b_offset += b_written;
                }
                else if (b_written < 0) {
                    ofLogError("ofxAudioDataWriterThread") << ofGetTimestampString("%H:%M:%S:%i") << " - write to PIPE failed with error -> " << errno << " - " << strerror(errno) << ".";
                    bNotifyError = true;
                    break;
                }
                else {
                    if(bClose){
                        // quit writing so we can close the file
                        break;
                    }
                }

                if (!isThreadRunning()) {
                    ofLogWarning("ofxAudioDataWriterThread") << ofGetTimestampString("%H:%M:%S:%i") << " - The thread is not running anymore let's get out of here!";
                }
            }
            bIsWriting = false;
            delete [] frame->data;
            delete frame;
        }
        else{
			conditionMutex.lock();
			condition.wait(conditionMutex);
			conditionMutex.unlock();
        }
    }

    ofLogVerbose("ofxAudioDataWriterThread") << "closing pipe: " <<  filePath;
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
	::close(fd);
#endif
#ifdef TARGET_WIN32
	FlushFileBuffers(audioHandle);
	DisconnectNamedPipe(audioHandle);
	CloseHandle(audioHandle);
#endif
}

//--------------------------------------------------------------
void ofxAudioDataWriterThread::signal(){
    condition.signal();
}

//--------------------------------------------------------------
void ofxAudioDataWriterThread::setPipeNonBlocking(){
    setNonBlocking(fd);
}

//--------------------------------------------------------------
//--------------------------------------------------------------
ofxVideoRecorder::ofxVideoRecorder(){
    bIsInitialized = false;
    ffmpegLocation = "ffmpeg";
    videoCodec = "mpeg4";
    audioCodec = "pcm_s16le";
    videoBitrate = "2000k";
    audioBitrate = "128k";
    pixelFormat = "rgb24";
}

//--------------------------------------------------------------
bool ofxVideoRecorder::setup(string fname, int w, int h, float fps, int sampleRate, int channels, bool sysClockSync, bool silent){
    if(bIsInitialized)
    {
        close();
    }

    fileName = fname;
    string absFilePath = ofFilePath::getAbsolutePath(fileName);

    moviePath = ofFilePath::getAbsolutePath(fileName);

    stringstream outputSettings;
	outputSettings
//     << " -vcodec " << videoCodec
//     << " -b:v " << videoBitrate
//     << " -acodec " << audioCodec
//     << " -ab " << audioBitrate
     << " " << absFilePath;

    return setupCustomOutput(w, h, fps, sampleRate, channels, outputSettings.str(), sysClockSync, silent);
}

//--------------------------------------------------------------
bool ofxVideoRecorder::setupCustomOutput(int w, int h, float fps, string outputString, bool sysClockSync, bool silent){
    return setupCustomOutput(w, h, fps, 0, 0, outputString, sysClockSync, silent);
}

//--------------------------------------------------------------
bool ofxVideoRecorder::setupCustomOutput(int w, int h, float fps, int sampleRate, int channels, string outputString, bool sysClockSync, bool silent){
    if(bIsInitialized)
    {
        close();
    }

    bIsSilent = silent;
    bSysClockSync = sysClockSync;

    bRecordAudio = (sampleRate > 0 && channels > 0);
    bRecordVideo = (w > 0 && h > 0 && fps > 0);
    bFinishing = false;

    videoFramesRecorded = 0;
    audioSamplesRecorded = 0;

    if(!bRecordVideo && !bRecordAudio) {
        ofLogWarning() << "ofxVideoRecorder::setupCustomOutput(): invalid parameters, could not setup video or audio stream.\n"
        << "video: " << w << "x" << h << "@" << fps << "fps\n"
        << "audio: " << "channels: " << channels << " @ " << sampleRate << "Hz\n";
        return false;
    }
    videoPipePath = "";
    audioPipePath = "";
    pipeNumber = requestPipeNumber();

#ifdef TARGET_WIN32
	HANDLE hVPipe;
	HANDLE hAPipe;
	LPTSTR vPipename;
	LPTSTR aPipename;
#endif

    if(bRecordVideo) {
        width = w;
        height = h;
        frameRate = fps;

#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
        // recording video, create a FIFO pipe
		videoPipePath = getVideoPipePath(pipeNumber);
        if(!ofFile::doesFileExist(videoPipePath)){
            string cmd = "bash --login -c 'mkfifo " + videoPipePath + "'";
            system(cmd.c_str());
            // TODO: add windows compatable pipe creation (does ffmpeg work with windows pipes?)
        }
#elif defined(TARGET_WIN32)

		char vpip[128];
		int num = ofRandom(1024);
		sprintf(vpip, "\\\\.\\pipe\\videoPipe%d", num);
		vPipename = convertCharArrayToLPCWSTR(vpip);

		hVPipe = CreateNamedPipe(
			vPipename, // name of the pipe
			PIPE_ACCESS_OUTBOUND, // 1-way pipe -- send only
			PIPE_TYPE_BYTE, // send data as a byte stream
			1, // only allow 1 instance of this pipe
			0, // outbound buffer defaults to system default
			0, // no inbound buffer
			0, // use default wait time
			NULL // use default security attributes
		);

		if (!(hVPipe != INVALID_HANDLE_VALUE)) {
			if (GetLastError() != ERROR_PIPE_BUSY)
			{
				ofLogError("Video Pipe") << "Could not open video pipe.";
			}
			// All pipe instances are busy, so wait for 5 seconds. 
			if (!WaitNamedPipe(vPipename, 5000))
			{
				ofLogError("Video Pipe") << "Could not open video pipe: 5 second wait timed out.";
			}
		}
#endif
    }

    if(bRecordAudio) {
        this->sampleRate = sampleRate;
        audioChannels = channels;

#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
        // recording video, create a FIFO pipe
		audioPipePath = getAudioPipePath(pipeNumber);
        if(!ofFile::doesFileExist(audioPipePath)){
            string cmd = "bash --login -c 'mkfifo " + audioPipePath + "'";
            system(cmd.c_str());

            // TODO: add windows compatable pipe creation (does ffmpeg work with windows pipes?)
        }
#elif defined(TARGET_WIN32)
		char apip[128];
		int num = ofRandom(1024);
		sprintf(apip, "\\\\.\\pipe\\videoPipe%d", num);


		aPipename = convertCharArrayToLPCWSTR(apip);

		hAPipe = CreateNamedPipe(
			aPipename,
			PIPE_ACCESS_OUTBOUND, // 1-way pipe -- send only
			PIPE_TYPE_BYTE, // send data as a byte stream
			1, // only allow 1 instance of this pipe
			0, // outbound buffer defaults to system default
			0, // no inbound buffer
			0, // use default wait time
			NULL // use default security attributes
		);

		if (!(hAPipe != INVALID_HANDLE_VALUE)) {
			if (GetLastError() != ERROR_PIPE_BUSY)
			{
				ofLogError("Audio Pipe") << "Could not open audio pipe.";
			}
			// All pipe instances are busy, so wait for 5 seconds. 
			if (!WaitNamedPipe(aPipename, 5000))
			{
				ofLogError("Audio Pipe") << "Could not open pipe: 5 second wait timed out.";
			}
		}
#endif
    }


	const auto& append_audio_cmd = [&](stringstream& cmd)
	{
		cmd << " -f s16le -acodec " << audioCodec << " -ar " << sampleRate << " -ac " << audioChannels;
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
		cmd << " -i " << audioPipePath;
#elif defined(TARGET_WIN32)
		cmd << " -i " << convertWideToNarrow(aPipename);
		cmd << " -b:a " << audioBitrate;
#endif
	};
	const auto& append_vedio_cmd = [&](stringstream& cmd)
	{
		cmd << " -r " << fps << " -s " << w << "x" << h << " -f rawvideo -pix_fmt " << pixelFormat;
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
		cmd << " -i " << videoPipePath;
#elif defined(TARGET_WIN32)
		cmd << " -i " << convertWideToNarrow(vPipename) << " -vcodec " << videoCodec << " -b:v " << videoBitrate;
#endif
	};

    stringstream cmd;
    // basic ffmpeg invocation, -y option overwrites output file
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
	cmd << "bash --login -c '" << ffmpegLocation << (bIsSilent?" -loglevel quiet ":" ") << "-y";
	if (bRecordAudio) {
		append_audio_cmd(cmd);
	}
	else { // no audio stream
		cmd << " -an";
	}
	if (bRecordVideo) { // video input options and file
		append_vedio_cmd(cmd);
	}
	else { // no video stream
		cmd << " -vn";
	}
	cmd << " " + outputString + "' &";

	// start ffmpeg thread. Ffmpeg will wait for input pipes to be opened.
	ffmpegThread.setup(cmd.str());
#endif


    // wait until ffmpeg has started
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
	while (!ffmpegThread.isInitialized()) {
		usleep(10);
	}
#endif


    if(bRecordAudio){
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
        audioThread.setup(audioPipePath, &audioFrames);
#endif
    }
    if(bRecordVideo){
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
        videoThread.setup(videoPipePath, &frames);
#endif
    }

	const auto& printError = [](string module, string message)
	{
#ifdef TARGET_WIN32
		LPTSTR errorText = NULL;
		FormatMessageW(
			// use system message tables to retrieve error text
			FORMAT_MESSAGE_FROM_SYSTEM
			// allocate buffer on local heap for error text
			| FORMAT_MESSAGE_ALLOCATE_BUFFER
			// Important! will fail otherwise, since we're not 
			// (and CANNOT) pass insertion parameters
			| FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,    // unused with FORMAT_MESSAGE_FROM_SYSTEM
			GetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR)&errorText,  // output 
			0, // minimum size for output buffer
			NULL);   // arguments - see note 

		wstring ws = errorText;
		string error(ws.begin(), ws.end());
		ofLogError(module) << message << error;
#endif
	};
	const auto& makeAudioPipe = [&]()
	{
#ifdef TARGET_WIN32
		//this blocks, so we have to call it after ffmpeg is listening for a pipe
		bool fSuccess = ConnectNamedPipe(hAPipe, NULL);
		if (!fSuccess)
		{
			printError("Audio Pipe", "SetNamedPipeHandleState failed: ");
		}
		else {
			ofLogNotice("Audio Pipe") << "\n==========================\nAudio Pipe Connected Successfully\n==========================\n" << endl;
			audioThread.setup(hAPipe, &audioFrames);
		}
#endif
	};
	const auto& makeVideoPipe = [&]()
	{
#ifdef TARGET_WIN32
		//this blocks, so we have to call it after ffmpeg is listening for a pipe
		bool fSuccess = ConnectNamedPipe(hVPipe, NULL);
		if (!fSuccess)
		{
			printError("Video Pipe", "SetNamedPipeHandleState failed: ");
		}
		else {
			ofLogNotice("Video Pipe") << "\n==========================\nVideo Pipe Connected Successfully\n==========================\n" << endl;
			videoThread.setup(hVPipe, &frames);
		}
#endif
	};
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
#elif defined(TARGET_WIN32)
	//evidently there are issues with multiple named pipes http://trac.ffmpeg.org/ticket/1663
	if (bRecordAudio && bRecordVideo) {
		// Audio Thread
		stringstream aCmd;
		aCmd << ffmpegLocation << " -y ";
		append_audio_cmd(aCmd);
		aCmd << " " << outputString << "_atemp" << audioFileExt;

		ffmpegAudioThread.setup(aCmd.str());
		ofLogNotice("FFMpeg Command") << aCmd.str() << endl;

		makeAudioPipe();

		// Video Thread
		stringstream vCmd;
		vCmd << ffmpegLocation << " -y ";
		append_vedio_cmd(vCmd);
		vCmd << " " << outputString << "_vtemp" << movFileExt;

		ffmpegVideoThread.setup(vCmd.str());
		ofLogNotice("FFMpeg Command") << vCmd.str() << endl;

		makeVideoPipe();
	}
	else {
		cmd << ffmpegLocation << " -y ";
		if (bRecordAudio) {
			append_audio_cmd(cmd);
		}
		else { // no audio stream
			cmd << " -an";
		}

		if (bRecordVideo) { // video input options and file
			append_vedio_cmd(cmd);
		}
		else { // no video stream
			cmd << " -vn";
		}

		cmd << " " << outputString << movFileExt;

		ofLogNotice("FFMpeg Command") << cmd.str() << endl;

		ffmpegThread.setup(cmd.str()); // start ffmpeg thread, will wait for input pipes to be opened

		if (bRecordAudio) {
			makeAudioPipe();
		}
		if (bRecordVideo) {
			makeVideoPipe();
		}
	}
#endif

    bIsInitialized = true;
    bIsRecording = false;
    bIsPaused = false;

    startTime = 0;
    recordingDuration = 0;
    totalRecordingDuration = 0;

    return bIsInitialized;
}

//--------------------------------------------------------------
bool ofxVideoRecorder::addFrame(const ofPixels &pixels){
    if (!bIsRecording || bIsPaused) return false;

    if(bIsInitialized && bRecordVideo && ffmpegThread.isInitialized())
    {
        int framesToAdd = 1; // default add one frame per request

        if((bRecordAudio || bSysClockSync) && !bFinishing){

            double syncDelta;
            double videoRecordedTime = videoFramesRecorded / frameRate;

            if (bRecordAudio) {
                // if also recording audio, check the overall recorded time for audio and video to make sure audio is not going out of sync
                // this also handles incoming dynamic framerate while maintaining desired outgoing framerate
                double audioRecordedTime = (audioSamplesRecorded/audioChannels)  / (double)sampleRate;
                syncDelta = audioRecordedTime - videoRecordedTime;
            }
            else {
                // if just recording video, synchronize the video against the system clock
                // this also handles incoming dynamic framerate while maintaining desired outgoing framerate
                syncDelta = systemClock() - videoRecordedTime;
            }

            if(syncDelta > 1.0/frameRate) {
                // not enought video frames, we need to send extra video frames.
                while(syncDelta > 1.0/frameRate) {
                    framesToAdd++;
                    syncDelta -= 1.0/frameRate;
                }
                ofLogVerbose() << "ofxVideoRecorder: recDelta = " << syncDelta << ". Not enough video frames for desired frame rate, copied this frame " << framesToAdd << " times.\n";
            }
            else if(syncDelta < -1.0/frameRate){
                // more than one video frame is waiting, skip this frame
                framesToAdd = 0;
                ofLogVerbose() << "ofxVideoRecorder: recDelta = " << syncDelta << ". Too many video frames, skipping.\n";
            }
        }

        for(int i=0;i<framesToAdd;i++){
            // add desired number of frames
            frames.Produce(new ofPixels(pixels));
            videoFramesRecorded++;
        }

        videoThread.signal();

        return true;
    }

    return false;
}

//--------------------------------------------------------------
void ofxVideoRecorder::addAudioSamples(float *samples, int bufferSize, int numChannels){
    if (!bIsRecording || bIsPaused) return;

    if(bIsInitialized && bRecordAudio){
        int size = bufferSize*numChannels;
        audioFrameShort * shortSamples = new audioFrameShort;
        shortSamples->data = new short[size];
        shortSamples->size = size;

        for(int i=0; i < size; i++){
            shortSamples->data[i] = (short)(samples[i] * 32767.0f);
        }
        audioFrames.Produce(shortSamples);
        audioThread.signal();
        audioSamplesRecorded += size;
    }
}

//--------------------------------------------------------------
void ofxVideoRecorder::start(){
    if(!bIsInitialized) return;

    if (bIsRecording) {
        // We are already recording. No need to go further.
       return;
    }

    // Start a recording.
    bIsRecording = true;
    bIsPaused = false;
    startTime = ofGetElapsedTimef();

    ofLogVerbose() << "Recording." << endl;
}

//--------------------------------------------------------------
void ofxVideoRecorder::setPaused(bool bPause){
    if(!bIsInitialized) return;

    if (!bIsRecording || bIsPaused == bPause) {
        //  We are not recording or we are already paused. No need to go further.
        return;
    }

    // Pause the recording
    bIsPaused = bPause;

    if (bIsPaused) {
        totalRecordingDuration += recordingDuration;

        // Log
        ofLogVerbose() << "Paused." << endl;
    } else {
        startTime = ofGetElapsedTimef();

        // Log
        ofLogVerbose() << "Recording." << endl;
    }
}

//--------------------------------------------------------------
void ofxVideoRecorder::close(){
    if(!bIsInitialized) return;

    bIsRecording = false;

    if(bRecordVideo && bRecordAudio) {
        // set pipes to non_blocking so we dont get stuck at the final writes
        // audioThread.setPipeNonBlocking();
        // videoThread.setPipeNonBlocking();

        if (frames.size() > 0 && audioFrames.size() > 0) {
            // if there are frames in the queue start a thread to finalize the output file without blocking the app.
            startThread();
            return;
        }
    }
    else if(bRecordVideo) {
        // set pipes to non_blocking so we dont get stuck at the final writes
        // videoThread.setPipeNonBlocking();

        if (frames.size() > 0) {
            // if there are frames in the queue start a thread to finalize the output file without blocking the app.
            startThread();
            return;
        }
        else {
            // cout << "ofxVideoRecorder :: we are good to go!" << endl;
        }

    }
    else if(bRecordAudio) {
        // set pipes to non_blocking so we dont get stuck at the final writes
        // audioThread.setPipeNonBlocking();

        if (audioFrames.size() > 0) {
            // if there are frames in the queue start a thread to finalize the output file without blocking the app.
            startThread();
            return;
        }
    }

    outputFileComplete();
}

//--------------------------------------------------------------
void ofxVideoRecorder::threadedFunction()
{
    if(bRecordVideo && bRecordAudio) {
        while(frames.size() > 0 && audioFrames.size() > 0) {
            // if there are frames in the queue or the thread is writing, signal them until the work is done.
            videoThread.signal();
            audioThread.signal();
        }
    }
    if(bRecordVideo) {
        while(frames.size() > 0) {
            // if there are frames in the queue or the thread is writing, signal them until the work is done.
            videoThread.signal();
        }
    }
    else if(bRecordAudio) {
        while(audioFrames.size() > 0) {
            // if there are frames in the queue or the thread is writing, signal them until the work is done.
            audioThread.signal();
        }
    }

    waitForThread();

    outputFileComplete();
}

//--------------------------------------------------------------
void ofxVideoRecorder::outputFileComplete()
{
    // at this point all data that ffmpeg wants should have been consumed
    // one of the threads may still be trying to write a frame,
    // but once close() gets called they will exit the non_blocking write loop
    // and hopefully close successfully

    bIsInitialized = false;

    if (bRecordVideo) {
        videoThread.close();
    }
    if (bRecordAudio) {
        audioThread.close();
    }

#ifdef TARGET_WIN32
	//at this point all data that ffmpeg wants should have been consumed
	// one of the threads may still be trying to write a frame,
	// but once close() gets called they will exit the non_blocking write loop
	// and hopefully close successfully

	if (bRecordAudio && bRecordVideo) {
		ffmpegAudioThread.waitForThread();
		ffmpegVideoThread.waitForThread();

		//need to do one last script here to join the audio and video recordings

		stringstream finalCmd;

		/*finalCmd << ffmpegLocation << " -y " << " -i " << filePath << "_vtemp" << movFileExt << " -i " << filePath << "_atemp" << movFileExt << " \\ ";
		finalCmd << "-filter_complex \"[0:0] [1:0] concat=n=2:v=1:a=1 [v] [a]\" \\";
		finalCmd << "-map \"[v]\" -map \"[a]\" ";
		finalCmd << " -vcodec " << videoCodec << " -b:v " << videoBitrate << " -b:a " << audioBitrate << " ";
		finalCmd << filePath << movFileExt;*/

		finalCmd << ffmpegLocation << " -y " << " -i " << moviePath << "_vtemp" << movFileExt << " -i " << moviePath << "_atemp" << audioFileExt << " ";
		finalCmd << "-c:v copy -c:a copy -strict experimental ";
		finalCmd << moviePath << movFileExt;

		ofLogNotice("FFMpeg Merge") << "\n==============================================\n Merge Command \n==============================================\n";
		ofLogNotice("FFMpeg Merge") << finalCmd.str();
		//ffmpegThread.setup(finalCmd.str());
		system(finalCmd.str().c_str());

		//delete the unmerged files
		stringstream removeCmd;
		ofStringReplace(moviePath, "/", "\\");
		removeCmd << "DEL " << moviePath << "_vtemp" << movFileExt << " " << moviePath << "_atemp" << audioFileExt;
		system(removeCmd.str().c_str());

	}
#endif

	retirePipeNumber(pipeNumber);

    ffmpegThread.waitForThread();
    // TODO: kill ffmpeg process if its taking too long to close for whatever reason.

    // Notify the listeners.
    ofxVideoRecorderOutputFileCompleteEventArgs args;
    args.fileName = fileName;
    ofNotifyEvent(outputFileCompleteEvent, args);
}

//--------------------------------------------------------------
bool ofxVideoRecorder::hasVideoError(){
    return videoThread.bNotifyError;
}

//--------------------------------------------------------------
bool ofxVideoRecorder::hasAudioError(){
    return audioThread.bNotifyError;
}

//--------------------------------------------------------------
float ofxVideoRecorder::systemClock(){
    recordingDuration = ofGetElapsedTimef() - startTime;
    return totalRecordingDuration + recordingDuration;
}

//--------------------------------------------------------------
set<int> ofxVideoRecorder::openPipes;

//--------------------------------------------------------------
int ofxVideoRecorder::requestPipeNumber(){
    int n = 0;
    while (openPipes.find(n) != openPipes.end()) {
        n++;
    }
    openPipes.insert(n);
    return n;
}

//--------------------------------------------------------------
void ofxVideoRecorder::retirePipeNumber(int num){
    if(!openPipes.erase(num)){
        ofLogNotice() << "ofxVideoRecorder::retirePipeNumber(): trying to retire a pipe number that is not being tracked: " << num << endl;
    }

    // remove pipe files
#if defined(TARGET_OSX) || defined(TARGET_LINUX)
    string videoPipe = getVideoPipePath(num);
    if (ofFile::doesFileExist(videoPipe)) {
        ofFile::removeFile(videoPipe);
    }
    string audioPipe = getAudioPipePath(num);
    if (ofFile::doesFileExist(audioPipe)) {
        ofFile::removeFile(audioPipe);
    }
#endif
}

string ofxVideoRecorder::getVideoPipePath(int num)
{
    return ofFilePath::getAbsolutePath("ofxvrpipe" + ofToString(num));
}

string ofxVideoRecorder::getAudioPipePath(int num)
{
    return ofFilePath::getAbsolutePath("ofxarpipe" + ofToString(num));
}