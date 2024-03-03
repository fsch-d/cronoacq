#include <iostream>
#include <regex>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>


#include "stdafx.h"
#include "Ndigo_common_interface.h"
#include "Ndigo_interface.h"
//#include "crono_tools.h"


#include "cronoCtrl.h"
#include "cronoacqDlg.h"

//root classes
#include <Riostream.h>
#include <TServerSocket.h>
#include <TSocket.h>



cronoCtrl::cronoCtrl() : 
	g_PauseACQ (FALSE, TRUE)
{
	CString log_msg;
	ndigoCount = 0;
	m_bAcq = false;
	m_bIsconfigured = false;
	m_bIsConnected = false;
	m_bFileOpen = false;
	m_bStatus = true;
	rate = 0;
	fsize = 0;
	m_bUseAdvConfig = false;

	start_serverThread(log_msg);
	g_PauseACQ.SetEvent();


	//starting sampling thread
	m_updateChartThread = AfxBeginThread(UpdateChartThread, this, THREAD_PRIORITY_NORMAL, CREATE_SUSPENDED);
	m_updateChartThread->m_bAutoDelete = FALSE;
	m_updateChartThread->ResumeThread();


	//starting status thread
	m_ReadStatusThread = AfxBeginThread(ReadStatusThread, this, THREAD_PRIORITY_NORMAL, CREATE_SUSPENDED);
	m_ReadStatusThread->m_bAutoDelete = FALSE;
	m_ReadStatusThread->ResumeThread();


	//allocate buffer memory
	event.trigger_index = 0;
	event.flags = 0;
	event.timestamp = 0;
	event.packet_count = 0;
	event.data = (char*)(malloc(MAX_NOF_ADCS * 16 * 1000 * 1000));

//	volatile ndigo_packet* i = (volatile ndigo_packet*)event.data;
//	if (event.data)
//		i->card = 1;

}


cronoCtrl::~cronoCtrl()
{
	m_bStatus = false;
	stop_acqThread();
	stop_serverThread();
	free(event.data);


	delete m_serverthread;

	//delete ss;

	int dwRes = WaitForSingleObject(m_updateChartThread->m_hThread, 1000);
	switch (dwRes)
	{
	case WAIT_OBJECT_0:
		TRACE(_T("chart thread just finished\r\n")); break;
	case WAIT_TIMEOUT:
		TRACE(_T("timed out, chart thread is still busy\r\n")); break;
	}
	delete m_updateChartThread;


	dwRes = WaitForSingleObject(m_ReadStatusThread->m_hThread, 200);
	switch (dwRes)
	{
	case WAIT_OBJECT_0:
		TRACE(_T("status thread just finished\r\n")); break;
	case WAIT_TIMEOUT:
		TRACE(_T("timed out, status thread is still busy\r\n")); break;
	}
	delete m_ReadStatusThread;

}

//This is done each time when new parameters are submitted
int cronoCtrl::init_acq(CString& log_msg)
{
	int error_code;
	const char *error_message;
	int status = 0;
	CString logmsg;
	CString strmsg, strError;



	//configration if needed.

	//Set up initialization struct with board 0 as master 
	//Find out how many Ndigo boards are present
	ndigoCount = ndigo_count_devices(&error_code, &error_message);

	if ((CString)error_message != _T("OK")) {
		log_msg = _T("Initialization error!!!");
		return 1;
	}

	log_msg = _T("The result of count_devices is: ") + (CString)error_message + _T("... ");
	strmsg.Format(_T("%d"), ndigoCount);
	log_msg = log_msg + _T("The number of boards is: ") + strmsg;


	//Who is the master?
	//m_initPars->main.sourcecard;

	for (int i = 0; i < ndigoCount; i++) {

		ndigo_get_default_init_parameters(&init_params[i]);
		init_params[i].card_index = i;
		init_params[i].version = NDIGO_API_VERSION;
		init_params[i].hptdc_sync_enabled = 0;
		init_params[i].board_id = i;
		init_params[i].drive_external_clock = (i == 0);
		init_params[i].use_external_clock = (i != 0);
		init_params[i].is_slave = (i != ((m_initPars->main.sourcecard < ndigoCount) ? m_initPars->main.sourcecard : 0));
		init_params[i].sync_period = 4;
		init_params[i].multiboard_sync = 1;
		init_params[i].buffer_size[0] = 1 << 23;
	}


	// Initialize all Ndigo boards
	for (int i = 0; i < ndigoCount; i++) ndigos[i] = ndigo_init(&init_params[i], &error_code, &error_message);

	if (error_code)
	{	
		strmsg.Format(_T("%d"), error_code);
		log_msg = log_msg + _T("\r\nError ") + strmsg + _T("during ADC - init: ") + (CString)error_message + _T("... ");

		return false;
	}

	///////////////////////////////////////////////////////////////////////////////
   // read general info from the ADC-card (serial number, firmware version, ...):
	for (int i = 0; i < ndigoCount; i++) ndigo_get_static_info(ndigos[i], &ndigo_info[i]);

	for (int i = 0; i < ndigoCount; i++) ndigo_set_board_id(ndigos[i], i);

	if (ndigoCount > 1)
	{
		for (int i = 0; i < ndigoCount; i++)
		{
			int serial = ndigo_info[i].board_serial; 
			int device_id = 0;

			for (int k = 0; k < ndigoCount; k++)
			{
				if (serial < ndigo_info[k].board_serial) device_id++;

			}
			ndigo_set_board_id(ndigos[i], device_id);
			PCI_ID_of_card[i] = device_id;
			cardNO_of_PCI_ID[device_id] = i;
		}
	}
	///////////////////////////////////////////////////////////////////////////////


	//Ndigos configure zero suppression etc. 
	for (int i = 0; i < ndigoCount; i++) {
		int PCI_ID = PCI_ID_of_card[i];
		status = ndigo_get_default_configuration(ndigos[i], &conf[PCI_ID]);

		conf[PCI_ID].adc_mode = NDIGO_ADC_MODE_ABCD; // 1.25Gs/s, 4 channels 

		conf[PCI_ID].trigger[NDIGO_TRIGGER_A0].threshold = (short)(m_initPars->card[PCI_ID].chan[0].thresh * 131);
		conf[PCI_ID].trigger[NDIGO_TRIGGER_A0].edge = m_initPars->card[PCI_ID].chan[0].edge;
		conf[PCI_ID].trigger[NDIGO_TRIGGER_A0].rising = m_initPars->card[PCI_ID].chan[0].rising;

		conf[PCI_ID].trigger[NDIGO_TRIGGER_B0].threshold = (short)(m_initPars->card[PCI_ID].chan[1].thresh * 131);
		conf[PCI_ID].trigger[NDIGO_TRIGGER_B0].edge = m_initPars->card[PCI_ID].chan[1].edge;
		conf[PCI_ID].trigger[NDIGO_TRIGGER_B0].rising = m_initPars->card[PCI_ID].chan[1].rising;

		conf[PCI_ID].trigger[NDIGO_TRIGGER_C0].threshold = (short)(m_initPars->card[PCI_ID].chan[2].thresh * 131);
		conf[PCI_ID].trigger[NDIGO_TRIGGER_C0].edge = m_initPars->card[PCI_ID].chan[2].edge;
		conf[PCI_ID].trigger[NDIGO_TRIGGER_C0].rising = m_initPars->card[PCI_ID].chan[2].rising;

		conf[PCI_ID].trigger[NDIGO_TRIGGER_D0].threshold = (short)(m_initPars->card[PCI_ID].chan[3].thresh * 131);
		conf[PCI_ID].trigger[NDIGO_TRIGGER_D0].edge = m_initPars->card[PCI_ID].chan[3].edge;
		conf[PCI_ID].trigger[NDIGO_TRIGGER_D0].rising = m_initPars->card[PCI_ID].chan[3].rising;

		conf[PCI_ID].trigger[NDIGO_TRIGGER_TDC].edge = m_initPars->card[PCI_ID].chan[4].edge; // edge trigger
		conf[PCI_ID].trigger[NDIGO_TRIGGER_TDC].rising = m_initPars->card[PCI_ID].chan[4].rising; // rising edge


		if (i == 0) {
			//die folgende Zeilen wurden von Till Jahnke empfohlen (MW)
			conf[PCI_ID].drive_bus[0] = NDIGO_TRIGGER_SOURCE_A0; //puts ion MCP signal on BUS0 such that all cards have access to it (MW)
			conf[PCI_ID].drive_bus[1] = NDIGO_TRIGGER_SOURCE_C0; //puts electron MCP signal on BUS1 such that all cards have access to it (MW)
			conf[PCI_ID].drive_bus[2] = NDIGO_TRIGGER_SOURCE_TDC; //puts projectile pulse signal on BUS2 such that all cards have access to it (MW)
		}
		conf[PCI_ID].trigger_block[0].gates = NDIGO_TRIGGER_GATE_NONE; // hardware gate
		conf[PCI_ID].trigger_block[1].gates = NDIGO_TRIGGER_GATE_NONE; // no hardware gate
		conf[PCI_ID].trigger_block[2].gates = NDIGO_TRIGGER_GATE_NONE; // no hardware gate
		conf[PCI_ID].trigger_block[3].gates = NDIGO_TRIGGER_GATE_NONE; // no hardware gate*/

		conf[PCI_ID].trigger_block[0].sources = NDIGO_TRIGGER_SOURCE_A0; // trigger source is channel A
		conf[PCI_ID].trigger_block[1].sources = NDIGO_TRIGGER_SOURCE_B0; // trigger source is channel A
		conf[PCI_ID].trigger_block[2].sources = NDIGO_TRIGGER_SOURCE_C0; // trigger source is channel A
		conf[PCI_ID].trigger_block[3].sources = NDIGO_TRIGGER_SOURCE_D0; // trigger source is channel A

		for (int k = 0; k < 4; k++) {

			//		conf[PCI_ID].trigger_block[k].sources = pow((float) 2, 2*k); // trigger source is channel A
			//		conf[PCI_ID].trigger_block[k].gates = NDIGO_TRIGGER_GATE_0;
			conf[PCI_ID].trigger_block[k].precursor = m_initPars->card[PCI_ID].cardPars.precursor; // precursor in in steps of 3.2ns. 
			conf[PCI_ID].trigger_block[k].length = m_initPars->card[PCI_ID].cardPars.length; // amount of samples to record after the trigger in 3.2ns steps
			conf[PCI_ID].trigger_block[k].retrigger = m_initPars->card[PCI_ID].cardPars.retrigger;
			conf[PCI_ID].trigger_block[k].enabled = true; // enable trigger
			conf[PCI_ID].trigger_block[k].minimum_free_packets = 1.0; //

			conf[PCI_ID].analog_offset[k] = 0; // position of the baseline. 0 is gnd. Range is ~ -0.22 bis +0.22. Correspnding to ~+-0.5V
		}
		conf[PCI_ID].dc_offset[0] = 0.4;
		conf[PCI_ID].tdc_enabled = true; // TDC
	}

	if (m_bUseAdvConfig) ReadAdvConfig();

	for (int i = 0; i < ndigoCount; i++) {
		int PCI_ID = PCI_ID_of_card[i];
		//int j = ndigoCount - i - 1; //swap ndigo cards
		ndigo_configure(ndigos[i], &conf[PCI_ID]); // write configuration to board
	}

	//for (int i = 0; i < ndigoCount; i++) ndigo_get_param_info(ndigos[i], &ndigo_paramInfo[i]);




/*	// prepare cronotools. cronotools takes care about multiple board sync + groups etc.
	//register Ndigos
	for (int i = 0; i < ndigoCount; i++) {

		devices[i].device_type = CRONO_DEVICE_NDIGO5G;
		devices[i].device = ndigos[i];
	}

	// cronotools init

	sync_init.device_count = ndigoCount;
	sync_init.devices = devices;

	sync = crono_sync_init(&sync_init, &error_code, &error_message);


	// configure cronotools (which channel is trigger, grouprange etc. (in ps))


	//sync_conf.trigger_card = ndigoCount - 1 - m_initPars->main.sourcecard; //swap ndigo cards
	sync_conf.trigger_card = m_initPars->main.sourcecard;
	sync_conf.trigger_channel = m_initPars->main.sourcechan;
	sync_conf.group_range_start = m_initPars->main.range_start; //the unit is ps
	sync_conf.group_range_end = m_initPars->main.range_stop;

	status = crono_sync_configure(sync, &sync_conf, &error_code, &error_message);



	if (status != CRONO_OK) {
		strError = error_message;
		strError = _T("Error in sync_configure: ") + strError + _T("!");
		log_msg = strError;
		//TRACE(strError);
		crono_sync_close(sync);
		return 1;
	}
	else {*/
		strmsg = _T(" boards successfully configured ... ");
		log_msg += strmsg;
		m_bIsconfigured = true;
	//}




	return 0;

}




void cronoCtrl::start_acqThread(CString & log_msg)
{
	if (!m_bIsconfigured)
	{
		log_msg += _T("\r\nCouldn't start acq. Ndigos not configured!");
		m_bAcq = FALSE;

	}
	else
	{
		m_bAcq = TRUE;
		rate = 0;
		m_acqthread = AfxBeginThread(acqThread, this, THREAD_PRIORITY_HIGHEST, CREATE_SUSPENDED);
		m_acqthread->m_bAutoDelete = FALSE;
		m_acqthread->ResumeThread();
		log_msg += _T("ACQ thread started");
	}

}

void cronoCtrl::stop_acqThread()
{
	if (!m_bAcq) return;

	g_PauseACQ.SetEvent();
	m_bAcq = FALSE;
	Sleep(1000);
//	
	int dwRes = WaitForSingleObject(m_acqthread->m_hThread, 2000);
	switch (dwRes)
	{
	case WAIT_OBJECT_0:
		TRACE(_T("acquisition thread just finished")); break;
	case WAIT_TIMEOUT:
		TRACE(_T("timed out, acquisition thread is still busy")); 
		//crono_sync_stop_capture(sync);
		//crono_sync_close(sync);
		// close Ndigos
		for (int m = 0; m < ndigoCount; m++) {
			ndigo_close(ndigos[m]);
		}
		break;
	}
	delete m_acqthread;

}


void cronoCtrl::reset_acq(CString& log_msg)
{
	stop_acqThread();
	init_acq(log_msg);
	start_acqThread(log_msg);
}

void cronoCtrl::start_serverThread(CString & log_msg)
{

	m_serverthread = AfxBeginThread(serverThread, this, THREAD_PRIORITY_NORMAL, CREATE_SUSPENDED);
	m_serverthread->m_bAutoDelete = FALSE;
	m_serverthread->ResumeThread();

//	AfxBeginThread(serverThread, NULL, THREAD_PRIORITY_NORMAL, 0, 0, NULL);
}

void cronoCtrl::stop_serverThread()
{
	CString log_msg;
	if (m_bServ) 
	{

		m_bServ = false;
		//g_NewBuffer.SetEvent();

		int dwRes = WaitForSingleObject(m_serverthread->m_hThread, 500);
		switch (dwRes)
		{
		case WAIT_OBJECT_0:
			log_msg.Format(_T("server thread # %d just finished\r\n"), 1);
			TRACE(log_msg); break;
		case WAIT_TIMEOUT:
		{
			log_msg.Format(_T("timed out, server thread # %d  is still busy\r\n"), 1);
			TRACE(log_msg); break;
			//ss->SetOption(kNoBlock, 1);
			//ss->Accept();
			//ss->Close();
			//ss->SetOption(kNoBlock, 0);
			break;
		}
		}

//		ss->Close();
		if (m_bIsConnected) s->Close();
		//	delete ss;

	}

	m_bIsConnected = FALSE;

}

void cronoCtrl::openfile(CString & filename)
{
	
	if (m_initPars->main.autosize) 
	{
		filenumber = 0;
		sfilename = filename;

		NextFile();
	}
	else
	{
		sfile.open(filename, std::ios_base::binary);

		if (!sfile)
		{
			const TCHAR* error_message = (LPCTSTR)(_T("failed to open ") + filename);
			throw std::system_error(errno, std::system_category(), (const char*)error_message);
		}
		else {
			sfile.clear();
			fsize = 0;
			g_PauseACQ.ResetEvent();
			Sleep(100);
			m_bFileOpen = true;
			sfilename = filename;
			sfullfilename = filename;
			g_PauseACQ.SetEvent();
			CString log_msg = _T("File ") + sfullfilename + _T(" opened.");
			AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)log_msg));
			int i = 0;
		}
	}
}

void cronoCtrl::closefile()
{
	if (sfile && m_bFileOpen) {
		m_bFileOpen = false;
		Sleep(200);
		sfile.close();
		CString log_msg = _T("File ") + sfullfilename + _T(" closed.");
		AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)log_msg));
	}
	else AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)L"No open file found."));
}

status_pars cronoCtrl::ReadStatus()
{
	status_pars mStatusPars;
	mStatusPars.iRate = rate;
	mStatusPars.bIsConnected = m_bIsConnected;
	mStatusPars.bIsRunning = m_bAcq;
	mStatusPars.bFileOpen = m_bFileOpen;

	rate = 0;
	return mStatusPars;
}

void cronoCtrl::PrintInitPars()
{
	CString log_msg;
	log_msg = m_initPars->main.autosize ? _T("Autosize=TRUE") : _T("Autosize=FALSE");
	AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
	log_msg.Format(_T("Max. filesize=%d"), m_initPars->main.filesize);
	AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));

}



///////////////////////////////////////////////////////////////////////////////////
///// Here are the private member functions and threads
///////////////////////////////////////////////////////////////////////////////////

//This here is the actual eventy loop
UINT cronoCtrl::acqThread(LPVOID pParam)
{ 
	CString logmsg;
	CString strmsg, strError;

	cronoCtrl* my_cronoCtrl = (cronoCtrl*)pParam;

	//int hit[15] = { 0 };

	int plotchan = -1;
	

	for (int i = 0; i < my_cronoCtrl->ndigoCount; i++) {
		__int32  ndigo_status = ndigo_start_capture(my_cronoCtrl->ndigos[my_cronoCtrl->PCI_ID_of_card[i]]);
		if (ndigo_status)
		{
			strError.Format(_T("Error in start_capture': error %d in device %d\r\n"), ndigo_status & 0xffffff, (ndigo_status & 0xf0000000) >> 24);
			TRACE(strError);
			my_cronoCtrl->m_bAcq = FALSE;
			return 2;
		}
		else {
			strmsg = _T("data acquisition started!\r\n");
			my_cronoCtrl->m_bAcq = TRUE;
			TRACE(strmsg);
		}
	}




	// for the graph call function directly?? 
	while (my_cronoCtrl->m_bAcq)
	{

		::WaitForSingleObject(my_cronoCtrl->g_PauseACQ, INFINITE);


		if (!my_cronoCtrl->read_data_from_ADC()) Sleep(5);
		else
		{

			evnt_group* group = &(my_cronoCtrl->event);

			if (group->trigger_index != -1)
			{
				my_cronoCtrl->rate += 1;


				//write group header to file
				int size = 3 * sizeof(int) + sizeof(__int64); // size of group header
				if (my_cronoCtrl->m_bFileOpen) {
					my_cronoCtrl->sfile.write((const char*)group, size ); //write group header to file
					my_cronoCtrl->fsize += size;
				}
				

				//deal with the group header for go4!
				bufferEvnt hdr;
				hdr.time = group->timestamp;
				if (group->packet_count < MAX_NOF_GO4PKTS) hdr.channel = group->packet_count;
				else hdr.channel = MAX_NOF_GO4PKTS;

				//deal with the group packets (i.e. single traces)!
				bufferEvnt buffer[MAX_NOF_GO4PKTS]; //online data for go4
				char* databuffer = group->data;
				for (int i = 0; i < group->packet_count; i++)
				{
					volatile ndigo_packet* packet = (volatile ndigo_packet *)databuffer;
					packet->timestamp -= group->timestamp;


					//write packet to file
					size = 4 * sizeof(unsigned char) + sizeof(unsigned int) + sizeof(__int64); // size of packet header
					size += packet->length * sizeof(unsigned __int64); // size of data in packet
					if (my_cronoCtrl->m_bFileOpen) {
						my_cronoCtrl->sfile.write((char*)packet, size); //write data packet to file
						my_cronoCtrl->fsize += size;
					}

					
					//extract times and fill buffer for online analysis

					__int64 packet_time = packet->timestamp;
					int channel = packet->card * 5 + packet->channel;

					if (i < hdr.channel)
					{
						buffer[i].channel = channel;
						if (packet->channel == 4) buffer[i].time = packet_time;
						else {
							short* data2 = (short*)&packet->data;
							
							
							/*int jmax = 0, datamax = 0;
							for (unsigned int j = 0; j < packet->length * 4; j++) {
								if (abs(*data2) > datamax) {
									datamax = abs(*data2);
									jmax = j;
									data2++;
								}
							}*/
							buffer[i].time = packet_time +time_CF(data2, packet->length * 4);
							if (packet->length > 18) 
								TRACE(_T("large length\r\n"));
						}
					}
					databuffer += sizeof(unsigned char) * 4 + sizeof(unsigned int) + (packet->length + 1) * sizeof(__int64);
				}
				
				//my_cronoCtrl->g_NewBuffer.SetEvent();
				
				//send buffer to socket
				if (my_cronoCtrl->m_bIsConnected && my_cronoCtrl->s != NULL)
				{
					int Flag_end = 0;
					int connectflag = my_cronoCtrl->s->SendRaw(&Flag_end, 4);
					if (my_cronoCtrl->s->SendRaw(&hdr, sizeof(bufferEvnt)) < 0) my_cronoCtrl->m_bIsConnected = false;
					if ((my_cronoCtrl->s->SendRaw(&buffer, sizeof(bufferEvnt)*hdr.channel) < 0)) my_cronoCtrl->m_bIsConnected = false;
				}


				if (my_cronoCtrl->m_bFileOpen && my_cronoCtrl->m_initPars->main.autosize && (my_cronoCtrl->sfile.tellp() > (static_cast<__int64>(my_cronoCtrl->m_initPars->main.filesize) << 20)) && my_cronoCtrl->m_initPars->main.filesize>0)
				{
					my_cronoCtrl->NextFile();//start a new file
				}

			}
		}
	}
	

	for (int m = 0; m < my_cronoCtrl->ndigoCount; m++) {
		ndigo_stop_capture(my_cronoCtrl->ndigos[m]);
		ndigo_close(my_cronoCtrl->ndigos[m]);
	}
	return 0;
} 


int cronoCtrl::read_data_from_ADC() // returns 0 if no data is present, else 1 
{
	int status = 0;
	int packet_count = 0;

	char* databuffer = event.data;

	event.trigger_index = -1;

	int trigcardNo = -1;
		

	//read main trigger card
	if (m_initPars->main.sourcecard >= 0 && m_initPars->main.sourcecard < ndigoCount) {
		trigcardNo = cardNO_of_PCI_ID[m_initPars->main.sourcecard];


		ndigo_read_in in;
		ndigo_read_out out;
		in.acknowledge_last_read = 0;
		int result = ndigo_read(ndigos[trigcardNo], &in, &out);

		if (!result) {
			status = 1;
			volatile ndigo_packet* packet = out.first_packet;


			//find main trigger
			while (packet <= out.last_packet && event.trigger_index == -1)
			{
				if (packet->type == NDIGO_PACKET_TYPE_16_BIT_SIGNED || packet->type == NDIGO_PACKET_TYPE_TDC_DATA) { //ADC channel
					if (event.trigger_index == -1 && packet->card == m_initPars->main.sourcecard && packet->channel == m_initPars->main.sourcechan) {
						event.trigger_index = packet->card * 5 + packet->channel;
						event.timestamp = packet->timestamp;
					}
					volatile ndigo_packet* next_packet = ndigo_next_packet(packet);
					packet = next_packet;
				}
			}

			//read data from trigger card
			packet = out.first_packet;
			while (packet <= out.last_packet)
			{
				if (packet->type == NDIGO_PACKET_TYPE_16_BIT_SIGNED || packet->type == NDIGO_PACKET_TYPE_TDC_DATA) { //ADC channel
					__int64 relative_time = packet->timestamp - event.timestamp;
					bool b_trig = (event.trigger_index != -1 && relative_time > m_initPars->main.range_start && relative_time < m_initPars->main.range_stop);
					if (event.trigger_index == 128) b_trig = true;

					//Plot traces
					if (!m_bTrigOnly || b_trig) {
						int channel = packet->card * 5 + packet->channel;
						if (channel == SelChan && (m_bNewWave)) {
							short* data2 = (short*)packet->data;
							for (unsigned int j = 0; j < 60 && j < packet->length * 4; j++)
							{
								YValues[j] = *data2 * 0.00762939453;
								data2++;
							}
							g_NewWave.SetEvent();
						}
					}

					if (b_trig) {
						if (packet->type == NDIGO_PACKET_TYPE_TDC_DATA) int Check = ndigo_process_tdc_packet(ndigos[trigcardNo], (ndigo_packet*)packet);
						size_t size = (size_t)((packet->length + 1) * sizeof(__int64) + sizeof(unsigned char) * 4 + sizeof(unsigned int));
						memcpy_s((void*)databuffer, size, (const void*)packet, size);
						databuffer += size;
						packet_count++;
					}
				}
				volatile ndigo_packet* next_packet = ndigo_next_packet(packet);
				ndigo_acknowledge(ndigos[trigcardNo], packet);
				packet = next_packet;
			}
		}
	}
	else event.trigger_index = 128;

	for (int i = 0; i < ndigoCount; i++)
		{
			if (i==trigcardNo) continue;

			ndigo_read_in in;
			ndigo_read_out out;
			in.acknowledge_last_read = 0;
			int result = ndigo_read(ndigos[i], &in, &out);



			if (result)	continue;
			status = 1;
			volatile ndigo_packet* packet = out.first_packet;

			//		int j = 0;
			while (packet <= out.last_packet)
			{
				if (packet->type == NDIGO_PACKET_TYPE_16_BIT_SIGNED || packet->type == NDIGO_PACKET_TYPE_TDC_DATA) { //ADC channel
					//Plot traces
					__int64 relative_time = packet->timestamp - event.timestamp;
					bool b_trig = (event.trigger_index != -1 && relative_time > m_initPars->main.range_start && relative_time < m_initPars->main.range_stop);
					if (event.trigger_index == 128) b_trig = true;


					if (!m_bTrigOnly || b_trig) {//plot traces
						int channel = packet->card * 5 + packet->channel;
						if (channel == SelChan && (m_bNewWave)) {
							short* data2 = (short*)packet->data;
							for (unsigned int j = 0; j < 60 && j < packet->length * 4; j++)
							{
								YValues[j] = *data2 * 0.00762939453;
								data2++;
							}
							g_NewWave.SetEvent();
						}
					}


					if (b_trig) {
						size_t size = (size_t)((packet->length + 1) * sizeof(__int64) + sizeof(unsigned char) * 4 + sizeof(unsigned int));
						memcpy_s((void*)databuffer, size, (const void*)packet, size);
						databuffer += size;
						packet_count++;
					}
				}
				volatile ndigo_packet* next_packet = ndigo_next_packet(packet);
				ndigo_acknowledge(ndigos[i], packet);
				packet = next_packet;
			}
		}
	event.packet_count = packet_count;

	return status;
}



UINT cronoCtrl::serverThread(LPVOID pParam)
{
	cronoCtrl* my_cronoCtrl = (cronoCtrl*)pParam;

	while (my_cronoCtrl->m_bServ) {
		{
			TServerSocket ss0(6004, kTRUE);
			TServerSocket *ss = &ss0;
			TSocket* s0;

			bool bsocket_ia=false;

			int Flag_end=1;
			s0 = ss->Accept();
			ss->Close();

			if (my_cronoCtrl->m_bIsConnected) {
				my_cronoCtrl->s->SendRaw(&Flag_end, 4);
				my_cronoCtrl->s->Close();
			}
			else my_cronoCtrl->m_bIsConnected = true;
			my_cronoCtrl->s = s0;

		}
	}

	return 0;
}

UINT cronoCtrl::UpdateChartThread(LPVOID pParam)
{
	cronoCtrl* my_cronoCtrl = (cronoCtrl*)pParam;

	TRACE(_T("entered chart thread\r\n"));


	while (1) {
		::WaitForSingleObject(my_cronoCtrl->g_NewWave, INFINITE);

		//for (int i = 0; i < 60; i++)
			//my_cronoacqDlg->YValues[i] = my_cronoacqDlg->m_cronoCtrl->YValues[i];
		AfxGetMainWnd()->SendMessage(WM_WAVE_NEW, 0, NULL);
	}
	return 0;
}

UINT cronoCtrl::ReadStatusThread(LPVOID pParam)
{
	cronoCtrl* my_cronoCtrl = (cronoCtrl*)pParam;

	status_pars mStatusPars;
	while (my_cronoCtrl->m_bStatus)
	{
	/*	if(!my_cronoCtrl->m_bAcq && my_cronoCtrl->m_bIsConnected)
		{
			//if (my_cronoCtrl->s->GetLastUsage) my_cronoCtrl->m_bIsConnected = false;
			//TTimeStamp time= my_cronoCtrl->s->GetLastUsage();
			CString msg;
			//msg.Format(_T("last usage in seconds: %d\r\n"),time.GetSec());
			TRACE(msg);
		}*/
		AfxGetMainWnd()->SendMessage(WM_STATUS_NEW, 0, NULL);
		Sleep(1000);
	}
	return 0;
}



void cronoCtrl::ReadAdvConfig()
{
	TCHAR currentDir[MAX_PATH];
	GetCurrentDirectory(MAX_PATH, currentDir);
	SetCurrentDirectory(LocalAppDataDir);

	CString strLine = _T("");
	CStdioFile file;
	if (file.Open(_T("NdigoAdvConfig.txt"), CFile::modeRead))
	{
		while (file.ReadString(strLine))
		{
			//replace common cronologic constants
			strLine.Replace(_T("NDIGO_ADC_MODE_ABCD"), _T("0"));
			strLine.Replace(_T("NDIGO_ADC_MODE_AC"), _T("4"));
			strLine.Replace(_T("NDIGO_ADC_MODE_BC"), _T("5"));
			strLine.Replace(_T("NDIGO_ADC_MODE_AD"), _T("6"));
			strLine.Replace(_T("NDIGO_ADC_MODE_BD"), _T("7"));
			strLine.Replace(_T("NDIGO_ADC_MODE_A"), _T("8"));
			strLine.Replace(_T("NDIGO_ADC_MODE_B"), _T("9"));
			strLine.Replace(_T("NDIGO_ADC_MODE_C"), _T("10"));
			strLine.Replace(_T("NDIGO_ADC_MODE_D"), _T("11"));
			strLine.Replace(_T("NDIGO_ADC_MODE_AAAA"), _T("12"));
			strLine.Replace(_T("NDIGO_ADC_MODE_BBBB"), _T("13"));
			strLine.Replace(_T("NDIGO_ADC_MODE_CCCC"), _T("14"));
			strLine.Replace(_T("NDIGO_ADC_MODE_DDDD"), _T("15"));
			strLine.Replace(_T("NDIGO_ADC_MODE_A12"), _T("28"));
			strLine.Replace(_T("NDIGO_ADC_MODE_B12"), _T("29"));
			strLine.Replace(_T("NDIGO_ADC_MODE_C12"), _T("30"));
			strLine.Replace(_T("NDIGO_ADC_MODE_D12"), _T("31"));
			strLine.Replace(_T("NDIGO_MAX_PRECURSOR"), _T("25"));
			strLine.Replace(_T("NDIGO_MAX_MULTISHOT"), _T("65535"));
			strLine.Replace(_T("NDIGO_FIFO_DEPTH"), _T("8176"));
			strLine.Replace(_T("NDIGO_OUTPUT_MODE_SIGNED16"), _T("0"));
			strLine.Replace(_T("NDIGO_OUTPUT_MODE_RAW"), _T("1"));
			strLine.Replace(_T("NDIGO_OUTPUT_MODE_CUSTOM"), _T("2"));
			strLine.Replace(_T("NDIGO_OUTPUT_MODE_CUSTOM_INL"), _T("3"));

			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_NONE"), _T("0x00000000"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_A0"), _T("0x00000001"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_A1"), _T("0x00000002"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_B0"), _T("0x00000004"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_B1"), _T("0x00000008"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_C0"), _T("0x00000010"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_C1"), _T("0x00000020"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_D0"), _T("0x00000040"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_D1"), _T("0x00000080"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_TDC"), _T("0x00000100"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_GATE"), _T("0x00000200"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_BUS0"), _T("0x00000400"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_BUS1"), _T("0x00000800"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_BUS2"), _T("0x00001000"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_BUS3"), _T("0x00002000"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_AUTO"), _T("0x00004000"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_ONE"), _T("0x00008000"));

			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_TDC_PE"), _T("0x01000000"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_GATE_PE"), _T("0x02000000"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_BUS0_PE"), _T("0x04000000"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_BUS1_PE"), _T("0x08000000"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_BUS2_PE"), _T("0x10000000"));
			strLine.Replace(_T("NDIGO_TRIGGER_SOURCE_BUS3_PE"), _T("0x20000000"));

			strLine.Replace(_T("NDIGO_TRIGGER_GATE_NONE"), _T("0x0000"));
			strLine.Replace(_T("NDIGO_TRIGGER_GATE_0"), _T("0x0001"));
			strLine.Replace(_T("NDIGO_TRIGGER_GATE_1"), _T("0x0002"));
			strLine.Replace(_T("NDIGO_TRIGGER_GATE_2"), _T("0x0004"));
			strLine.Replace(_T("NDIGO_TRIGGER_GATE_3"), _T("0x0008"));

			strLine.Replace(_T("NDIGO_BUFFER_ALLOCATE"), _T("0"));
			strLine.Replace(_T("NDIGO_BUFFER_USE_PHYSICAL"), _T("1"));
			strLine.Replace(_T("NDIGO_BUFFER_USE_PREALLOCATED"), _T("2"));

			strLine.Replace(_T("NDIGO_TRIGGER_A0"), _T("0"));
			strLine.Replace(_T("NDIGO_TRIGGER_A1"), _T("1"));
			strLine.Replace(_T("NDIGO_TRIGGER_B0"), _T("2"));
			strLine.Replace(_T("NDIGO_TRIGGER_B1"), _T("3"));
			strLine.Replace(_T("NDIGO_TRIGGER_C0"), _T("4"));
			strLine.Replace(_T("NDIGO_TRIGGER_C1"), _T("5"));
			strLine.Replace(_T("NDIGO_TRIGGER_D0"), _T("6"));
			strLine.Replace(_T("NDIGO_TRIGGER_D1"), _T("7"));
			strLine.Replace(_T("NDIGO_TRIGGER_TDC"), _T("8"));
			strLine.Replace(_T("NDIGO_TRIGGER_GATE"), _T("9"));
			strLine.Replace(_T("NDIGO_TRIGGER_BUS0"), _T("10"));
			strLine.Replace(_T("NDIGO_TRIGGER_BUS1"), _T("11"));
			strLine.Replace(_T("NDIGO_TRIGGER_BUS2"), _T("12"));
			strLine.Replace(_T("NDIGO_TRIGGER_BUS3"), _T("13"));
			strLine.Replace(_T("NDIGO_TRIGGER_AUTO"), _T("14"));
			strLine.Replace(_T("NDIGO_TRIGGER_ONE"), _T("15"));

			strLine.Replace(_T("NDIGO_TRIGGER_TDC_PE"), _T("16"));

			strLine.Replace(_T("NDIGO_TRIGGER_GATE_PE"), _T("17"));
			strLine.Replace(_T("NDIGO_TRIGGER_BUS0_PE"), _T("18"));
			strLine.Replace(_T("NDIGO_TRIGGER_BUS1_PE"), _T("19"));
			strLine.Replace(_T("NDIGO_TRIGGER_BUS2_PE"), _T("20"));
			strLine.Replace(_T("NDIGO_TRIGGER_BUS3_PE"), _T("21"));


			int Position = 0;
			CString Token;
			CString log_msg;
			int i = -1;
			int k = -1;

			strLine = strLine.Tokenize(_T(";"), Position);
			strLine = strLine.Trim();
			Position = 0;
			Token = strLine.Tokenize(_T("."), Position);
			int Position0 = Position;

			if(swscanf_s(Token, _T("conf[%d]"), &i) == 1 && i>=0 && i<ndigoCount) {
				Token = strLine.Tokenize(_T("."), Position);
				int Position1 = Position;
				if (Token.Left(4) == _T("size")) {
					int value = GetIntValue(strLine.Mid(Position0));
					conf[i].size = value;
					log_msg.Format(_T("conf[%d].size = %d"), i, value);
					AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
				}
				else if (Token.Left(7) == _T("version")) {
					int value = GetIntValue(strLine.Mid(Position0));
					conf[i].version = value;
					log_msg.Format(_T("conf[%d].version = %d"), i, value);
					AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
				}
				else if (Token.Left(8) == _T("adc_mode")) {
					int value = GetIntValue(strLine.Mid(Position0));
					conf[i].adc_mode = value;
					log_msg.Format(_T("conf[%d].adc_mode = %d"), i, value);
					AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
				}
				else if (Token.Left(9) == _T("bandwidth")) {
					double value = GetDblValue(strLine.Mid(Position0));
					conf[i].bandwidth = value;
					log_msg.Format(_T("conf[%d].bandwidth = %f"), i, value);
					AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
				}
				else if (Token.Left(11) == _T("tdc_enabled")) {
					int value = GetIntValue(strLine.Mid(Position0));
					conf[i].tdc_enabled = value == 1 ? TRUE : FALSE;
					log_msg.Format(_T("conf[%d].tdc_enabled = %d"), i, value);
					AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
				}
				else if (Token.Left(14) == _T("tdc_fb_enabled")) {
					int value = GetIntValue(strLine.Mid(Position0));
					conf[i].tdc_fb_enabled = value == 1 ? TRUE : FALSE;
					log_msg.Format(_T("conf[%d].tdc_fb_enabled = %d"), i, value);
					AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
				}
				else if (Token.Left(13) == _T("analog_offset")) {
					double value = GetDblValue(strLine.Mid(Position0));
					int j = GetIndex(Token);
					if (j > 0 && j < NDIGO_CHANNEL_COUNT) {
						conf[i].analog_offset[j] = value;
						log_msg.Format(_T("conf[%d].analog_offset[%d] = %f"), i, j, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
				}
				else if (Token.Left(9) == _T("dc_offset")) {
					double value = GetDblValue(strLine.Mid(Position0));
					int j = GetIndex(Token);
					if (j > 0 && j < 2) {
						conf[i].dc_offset[j] = value;
						log_msg.Format(_T("conf[%d].dc_offset[%d] = %f"), i, j, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
				}
				else if (Token.Left(9) == _T("drive_bus")) {
					int value = GetIntValue(strLine.Mid(Position0));
					int j = GetIndex(Token);
					if (j > 0 && j < 4) {
						conf[i].drive_bus[j] = value;
						log_msg.Format(_T("conf[%d].drive_bus[%d] = %d"), i, j, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
				}
				else if (Token.Left(19) == _T("auto_trigger_period")) {
					int value = GetIntValue(strLine.Mid(Position0));
					conf[i].auto_trigger_period = value;
					log_msg.Format(_T("conf[%d].auto_trigger_period = %d"), i, value);
					AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
				}
				else if (Token.Left(28) == _T("auto_trigger_random_exponent")) {
					int value = GetIntValue(strLine.Mid(Position0));
					conf[i].auto_trigger_random_exponent = value;
					log_msg.Format(_T("conf[%d].auto_trigger_random_exponent = %d"), i, value);
					AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
				}
				else if (Token.Left(11) == _T("output_mode")) {
					int value = GetIntValue(strLine.Mid(Position0));
					conf[i].output_mode = value;
					log_msg.Format(_T("conf[%d].output_mode = %d"), i, value);
					AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
				}
				else if (swscanf_s(Token, _T("trigger[%d]"), &k) == 1 && k >= 0 && k < NDIGO_TRIGGER_COUNT + NDIGO_ADD_TRIGGER_COUNT)
				{
					Token = strLine.Tokenize(_T("."), Position);
					if (Token.Left(9) == _T("threshold")) {
						int value = GetIntValue(strLine.Mid(Position1));
						if (value >= -32768 && value <= 32768) {
							conf[i].trigger[k].threshold = (short)value;
							log_msg.Format(_T("conf[%d].trigger[%d].threshold = %d"), i, k, value);
							AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
						}
					}
					if (Token.Left(4) == _T("edge")) {
						int value = GetIntValue(strLine.Mid(Position1));
						conf[i].trigger[k].edge = (value == 1 ? TRUE : FALSE);
						log_msg.Format(_T("conf[%d].trigger[%d].edge = %d"), i, k, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
					if (Token.Left(6) == _T("rising")) {
						int value = GetIntValue(strLine.Mid(Position1));
						conf[i].trigger[k].rising = (value == 1 ? TRUE : FALSE);
						log_msg.Format(_T("conf[%d].trigger[%d].rising = %d"), i, k,value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
				}
				else if (swscanf_s(Token, _T("trigger_block[%d]"), &k) == 1 && k >= 0 && k < NDIGO_CHANNEL_COUNT + 1)
				{
					Token = strLine.Tokenize(_T("."), Position);
					if (Token.Left(7) == _T("enabled")) {
						int value = GetIntValue(strLine.Mid(Position1));
						conf[i].trigger_block[k].enabled = (value == 1 ? TRUE : FALSE);
						log_msg.Format(_T("conf[%d].trigger_block[%d].enabled = %d"), i, k, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
					if (Token.Left(9) == _T("force_led")) {
						int value = GetIntValue(strLine.Mid(Position1));
						conf[i].trigger_block[k].force_led = (value == 1 ? TRUE : FALSE);
						log_msg.Format(_T("conf[%d].trigger_block[%d].force_led = %d"), i, k, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
					if (Token.Left(9) == _T("retrigger")) {
						int value = GetIntValue(strLine.Mid(Position1));
						conf[i].trigger_block[k].retrigger = (value == 1 ? TRUE : FALSE);
						log_msg.Format(_T("conf[%d].trigger_block[%d].retrigger = %d"), i, k, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
					if (Token.Left(16) == _T("multi_shot_count")) {
						int value = GetIntValue(strLine.Mid(Position1));
						conf[i].trigger_block[k].multi_shot_count = value;
						log_msg.Format(_T("conf[%d].trigger_block[%d].multi_shot_count = %d"), i, k, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
					if (Token.Left(9) == _T("precursor")) {
						int value = GetIntValue(strLine.Mid(Position1));
						conf[i].trigger_block[k].precursor = value;
						log_msg.Format(_T("conf[%d].trigger_block[%d].precursor = %d"), i, k, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
					if (Token.Left(6) == _T("length")) {
						int value = GetIntValue(strLine.Mid(Position1));
						conf[i].trigger_block[k].length = value;
						log_msg.Format(_T("conf[%d].trigger_block[%d].length = %d"), i, k, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
					if (Token.Left(7) == _T("sources")) {
						int value = GetIntValue(strLine.Mid(Position1));
						conf[i].trigger_block[k].sources = value;
						log_msg.Format(_T("conf[%d].trigger_block[%d].sources = %d"), i, k, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
					if (Token.Left(5) == _T("gates")) {
						int value = GetIntValue(strLine.Mid(Position1));
						conf[i].trigger_block[k].gates = value;
						log_msg.Format(_T("conf[%d].trigger_block[%d].gates = %d"), i, k, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
					if (Token.Left(20) == _T("minimum_free_packets")) {
						double value = GetDblValue(strLine.Mid(Position1));
						conf[i].trigger_block[k].minimum_free_packets = value;
						log_msg.Format(_T("conf[%d].trigger_block[%d].minimum_free_packets = %f"), i, k, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
				}
				else if (swscanf_s(Token, _T("gating_block[%d]"), &k) == 1 && k >= 0 && k < NDIGO_GATE_COUNT)
				{
					Token = strLine.Tokenize(_T("."), Position);
					if (Token.Left(6) == _T("negate")) {
						int value = GetIntValue(strLine.Mid(Position1));
						conf[i].gating_block[k].negate = (value == 1 ? TRUE : FALSE);
						log_msg.Format(_T("conf[%d].gating_block[%d].negate = %d"), i, k, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
					if (Token.Left(9) == _T("retrigger")) {
						int value = GetIntValue(strLine.Mid(Position1));
						conf[i].gating_block[k].retrigger = (value == 1 ? TRUE : FALSE);
						log_msg.Format(_T("conf[%d].gating_block[%d].retrigger = %d"), i, k, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
					if (Token.Left(6) == _T("extend")) {
						int value = GetIntValue(strLine.Mid(Position1));
						conf[i].gating_block[k].extend = (value == 1 ? TRUE : FALSE);
						log_msg.Format(_T("conf[%d].gating_block[%d].extend = %d"), i, k, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
					if (Token.Left(5) == _T("start")) {
						int value = GetIntValue(strLine.Mid(Position1));
						conf[i].gating_block[k].start = value;
						log_msg.Format(_T("conf[%d].gating_block[%d].start = %d"), i, k, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
					if (Token.Left(4) == _T("stop")) {
						int value = GetIntValue(strLine.Mid(Position1));
						conf[i].gating_block[k].stop = value;
						log_msg.Format(_T("conf[%d].gating_block[%d].stop = %d"), i, k, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
					if (Token.Left(7) == _T("sources")) {
						int value = GetIntValue(strLine.Mid(Position1));
						conf[i].gating_block[k].sources = value;
						log_msg.Format(_T("conf[%d].gating_block[%d].sources = %d"), i, k, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
				}
				else if (swscanf_s(Token, _T("extension_block[%d]"), &k) == 1 && k >= 0 && k < NDIGO_EXTENSION_COUNT)
				{
					Token = strLine.Tokenize(_T("."), Position);
					if (Token.Left(6) == _T("enable")) {
						int value = GetIntValue(strLine.Mid(Position1));
						conf[i].extension_block[k].enable = (value == 1 ? TRUE : FALSE);
						log_msg.Format(_T("conf[%d].extension_block[%d].enable = %d"), i, k, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
					if (Token.Left(12) == _T("ignore_cable")) {
						int value = GetIntValue(strLine.Mid(Position1));
						conf[i].extension_block[k].ignore_cable = (value == 1 ? TRUE : FALSE);
						log_msg.Format(_T("conf[%d].extension_block[%d].ignore_cable = %d"), i, k, value);
						AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)(log_msg)));
					}
				}
			}
		}
	}
	else {
		AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)L"Couldn't find advanced configuration file NdigoAdvConfig.txt"));
		SetCurrentDirectory(currentDir);
		return;
	}
	

	file.Close();
	SetCurrentDirectory(currentDir);

}


int cronoCtrl::GetIntValue(CString Token)
{
	int i;
	int Position = 0;
	CString svalue;

	if (Token.Find(_T("=")) == -1) return -1;
	Token.Tokenize(_T("="), Position);
	if (Position == -1) return -1;

	svalue = Token.Tokenize(_T(""), Position);
	svalue = svalue.Trim();

	if (svalue.CompareNoCase(_T("true")) == 0) return 1;
	if (svalue.CompareNoCase(_T("false")) == 0) return 0;
	if (svalue.Left(2) == _T("0x")) swscanf_s(svalue, _T("%x"), &i);
	else swscanf_s(svalue, _T("%d"), &i);

	return i;
}

double cronoCtrl::GetDblValue(CString Token)
{
	double f;
	int Position = 0;
	CString svalue;
	
	if (Token.Find(_T("=")) == -1) return -9999;
	Token.Tokenize(_T("="), Position);
	if (Position == -1) return -9999;
	svalue = Token.Tokenize(_T("="), Position);
	svalue = svalue.Trim();

	swscanf_s(svalue, _T("%lf"), &f);

	return f;
}

int cronoCtrl::GetIndex(CString Token)
{
	CString pIndex;
	int Pos1, Pos2;

	if( (Pos1=Token.Find(_T("[")))==-1 || (Pos2 = Token.Find(_T("]"))) == -1) return -1;
	pIndex = Token.Mid(Pos1 + 1, Pos2 - Pos1 - 1);

	int i = _wtoi(pIndex);
	return i;
}

void cronoCtrl::NextFile()
{
	CFileStatus status;
	CString numberstring;

	closefile();


	numberstring.Format(_T("%04d"),filenumber);
	sfullfilename = sfilename;
	sfullfilename.Delete(sfullfilename.GetLength() - 5, 5);
	sfullfilename += numberstring + _T(".daqb");

	while (CFile::GetStatus(sfullfilename, status) && filenumber < 10000 )
	{
		filenumber++;
		numberstring.Format(_T("%04d"), filenumber);
		sfullfilename.Delete(sfullfilename.GetLength() - 9, 9);
		sfullfilename += numberstring + _T(".daqb");
	}


	if (!(filenumber < 10000)) return;
	sfile.open(sfullfilename, std::ios_base::binary);

	if (!sfile)
	{
		const TCHAR* error_message = (LPCTSTR)(_T("failed to open ") + sfullfilename);
		throw std::system_error(errno, std::system_category(), (const char*)error_message);
	}
	else {
		fsize = 0;
		Sleep(100);
		m_bFileOpen = true;
		CString log_msg = _T("File ") + sfullfilename + _T(" opened.");
		AfxGetMainWnd()->SendMessage(WM_LOGMSG, 0, (LPARAM)((LPCTSTR)log_msg));
	}
}



__int64 cronoCtrl::time_CF(short* data_buf, int size)
{
	int max = 0, imax = 0;
	double threshold = 0;
	__int64 found_time = 0;


	for (int i = 0; i < size; i++)
	{
		if (abs(data_buf[i]) > max)
		{
			max = abs(data_buf[i]);
			imax = i;
		}
	}
	threshold = (double)max * 0.3;

	for (int j = imax; j >= 0; j--)
	{
		if (fabs(data_buf[j - 1]) < threshold&&fabs(data_buf[j]) >= threshold)
		{
			found_time = (__int64)(800.0*((double)j - 1. + (-fabs((double)data_buf[j - 1]) + (double)threshold) / (fabs((double)data_buf[j]) - fabs((double)data_buf[j - 1]))));
			break;
		}
	}
	return (__int64)found_time;
	
}







