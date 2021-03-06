/**
* Copyright 2017 IBM Corp. All Rights Reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
*/


#include "NaoCamera.h"
#include "SelfInstance.h"

#ifndef _WIN32
#include <qi/os.hpp>
#include <alvision/alvisiondefinitions.h>
#include <alvision/alimage.h>
#include <alproxies/alvideodeviceproxy.h>
#endif

#include "tinythread++/tinythread.h"
#include "utils/JpegHelpers.h"

#ifndef _WIN32
REG_SERIALIZABLE(NaoCamera);
REG_OVERRIDE_SERIALIZABLE( Camera, NaoCamera);
#endif

RTTI_IMPL(NaoCamera, Camera);

bool NaoCamera::OnStart()
{
	Log::Debug("NaoVideo", "Starting up video device");

	m_StopThread = false;
	ThreadPool::Instance()->InvokeOnThread<void *>( DELEGATE(NaoCamera, StreamingThread, void *, this ), NULL );
	return true;
}

bool NaoCamera::OnStop()
{
	m_StopThread = true;
	while(! m_ThreadStopped )
		tthread::this_thread::yield();
	return true;
}

void NaoCamera::StreamingThread(void * arg)
{
	try
	{
		DoStreamingThread(arg);
	}
	catch( const std::exception & ex )
	{
		Log::Error( "NaoCamera", "Caught Exception: %s", ex.what() );
	}
	m_ThreadStopped = true;
}

void NaoCamera::DoStreamingThread(void *arg)
{
#ifndef _WIN32
	std::string robotIp("127.0.0.1");
	SelfInstance * pInstance = SelfInstance::GetInstance();
	if ( pInstance != NULL )
		robotIp = URL(pInstance->GetLocalConfig().m_RobotUrl).GetHost();

	AL::ALVideoDeviceProxy  camProxy(robotIp.c_str(), 9559);
	m_ClientName = camProxy.subscribeCamera(m_ClientName, 0, 1/*AL::kQVGA*/, 11 /*AL::kRGBColorSpace*/, 10 );

	AL::ALValue lImage;
	lImage.arraySetSize(7);

	while(!m_StopThread)
	{
		if ( m_Paused == 0 )
		{
			AL::ALValue img = camProxy.getImageRemote(m_ClientName);
			if(img.getSize() != 12) 
			{
				Log::Error("NaoCamera", "Image Size: %d", img.getSize());
				boost::this_thread::sleep(boost::posix_time::milliseconds(3000));
				continue;
			}

			const unsigned char * pRGB = (const unsigned char *)img[6].GetBinary();
			if ( pRGB == NULL )
			{
				Log::Error("NaoCamera", "Failed to get remote image." );
				boost::this_thread::sleep(boost::posix_time::milliseconds(3000));
				continue;
			}

			int width = (int)img[0];
			int height = (int)img[1];
			int depth = (int)img[2];

			Log::Debug( "NaoCamera", "Grabbed image %d x %d x %d", width, height, depth );

			std::string encoded;
			if ( JpegHelpers::EncodeImage( pRGB, width, height, depth, encoded ) )
			{
				ThreadPool::Instance()->InvokeOnMain<VideoData *>(
					DELEGATE( NaoCamera, SendingData, VideoData *, this ), new VideoData(encoded));
			}
			else
				Log::Error( "NaoCamera", "Failed to imencode()" );

			camProxy.releaseImage(m_ClientName);
		}

		boost::this_thread::sleep(boost::posix_time::milliseconds(1000 / m_fFramesPerSec));
	}

	Log::Debug("NaoVideo", "Closing Video feed with m_ClientName: %s", m_ClientName.c_str());
	camProxy.unsubscribe(m_ClientName);
	Log::Status("NaoVideo", "Stopped video device");
#endif
}

void NaoCamera::SendingData( VideoData * a_pData )
{
	SendData( a_pData );
}

void NaoCamera::OnPause()
{
	m_Paused++;
}

void NaoCamera::OnResume()
{
	m_Paused--;
}


