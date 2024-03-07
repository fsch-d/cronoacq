#include "acqcontrol.h"
#include <QTimer>
#include <QThread>
#include <QRandomGenerator>
#include <QtMath>
#include <QCoreApplication>
#include <memory.h>
#include <QtNetwork>



#include "Ndigo_common_interface.h"
#include "Ndigo_interface.h"
#include "Ndigo250M_interface.h"




acqcontrol::acqcontrol(QObject *parent)
    : QObject{parent}
{
    m_isrunning=false;
    m_isinitialized=false;
    m_count=0;
    m_nofsamples=0;
    m_timercreated_flag=false;
    m_sampleNow=false;
    m_writetofile=false;



    QObject::connect(this,&acqcontrol::startloop,this,&acqcontrol::acqloop,Qt::QueuedConnection);


    //allocate buffer memory (obsolete)
    event.flags = 0;
    event.timestamp = 0;
    event.packet_count = 0;

    filesize=0;



   qInfo() << this << "acqCtrl created ...";

}

acqcontrol::~acqcontrol()
{
    qInfo() << this << "acqcontrol destructor ...";

    for (int m = 0; m < ndigoCount + ndigo250mCount; m++) ndigo_close(ndigos[m]);




    emit finished();
}

void acqcontrol::createtimers()
{
    if(m_timercreated_flag) return;
    rateTimer = new QTimer(this);
    QObject::connect(rateTimer,&QTimer::timeout,this,&acqcontrol::RateTrigger);
    QObject::connect(this,&acqcontrol::finished, rateTimer, &QTimer::stop, Qt::DirectConnection);
    QObject::connect(this, &acqcontrol::destroyed, rateTimer , &QTimer::deleteLater);
    rateTimer->start(1000);

    plotTimer = new QTimer(this);
    QObject::connect(plotTimer,&QTimer::timeout,this,&acqcontrol::PlotTrigger);
    QObject::connect(this,&acqcontrol::finished, plotTimer, &QTimer::stop, Qt::DirectConnection);
    QObject::connect(this, &acqcontrol::destroyed, plotTimer , &QTimer::deleteLater);
    plotTimer->start(1000/maxfps);

    m_timercreated_flag=true;


    qInfo() << this << "timers created";

}

void acqcontrol::acqloop()
{
    QString strError;
    int trigcardNo = initpars.main.sourcecard;


    m_isrunning=true;
    if(!m_timercreated_flag) createtimers();

    if(initAcq()!=0){
        m_isrunning=false;
        emit errormessage(tr("Unable to initialize cards!"));
    }




    //relative timing is sensitive to the order
    //card with clock master (i.e., card0) needs to go last...

    for (int i = ndigoCount + ndigo250mCount - 1; i >= 0 && m_isrunning; i--)
    {
        qint32  ndigo_status=-1;
        ndigo_status= ndigo_start_capture(ndigos[i]);
        if (ndigo_status)
        {
            strError = tr("Error in start_capture': error %1 in device %2").arg(ndigo_status & 0xffffff).arg((ndigo_status & 0xf0000000) >> 24);
            emit errormessage(strError);
            m_isrunning = false;
            return;
        }
    }

    emit logmessage(tr("ACQ loop started!"));


    while(m_isrunning)
    {
        //char* databuffer = event.data;
        event.validTrigger = false;
        event.packet_count = 0;

        bufferEvnt trigger;
        trigger.channel=-1;
        trigger.time=0;

//        m_count++;
//        QCoreApplication::processEvents();
//        continue;


        bool b_nodata=true; // if true after eventloop, sleep for a few millseconds

        ndigo_read_in in;
        ndigo_read_out out;
        in.acknowledge_last_read = true;


        ndigo250m_read_in in250m;
        in250m.acknowledge_last_read = true;
        in250m.mask = 0;
        ndigo250m_read_out out250m;



        int readoutErr = -1;
        if(initpars.main.sourcecard >= 0 && initpars.main.sourcecard < ndigoCount) readoutErr = ndigo_read(ndigos[trigcardNo], &in, &out); //read out trigger card


        if (readoutErr == 0) // check for a valid main trigger and find time
        {
            volatile ndigo_packet* packet = out.first_packet;
            b_nodata = false;
            while (packet <= out.last_packet)
            {
                if (packet->card == initpars.main.sourcecard && packet->channel == initpars.main.sourcechan)
                {
                    event.validTrigger = true;
                    event.timestamp = packet->timestamp;
                    trigger.time = packet->timestamp;
                    trigger.channel = packet->card * 5 + packet->channel;
                    m_count++;
                    break;
                }
                volatile ndigo_packet* next_packet = ndigo_next_packet(packet);
                packet = next_packet;
            }
        }

        ////////////////////////////////////////////
        /// \brief prepare go4 buffer and datastream
        ////////////////////////////////////////////
        QByteArray go4buffer;
        go4buffer.clear();
        go4buffer.reserve(GO4_BUFFERSIZE*sizeof(bufferEvnt));
        QDataStream dataBffr(&go4buffer, QIODevice::ReadWrite);
        int nof_signals = 0;


        ////////////////////////////////////////
        /// \brief declare datastream for file
        /////////////////////////////////////////
        QDataStream fileOut(sFile);


        int readoutErr250m = -1;

        //////////////////////////////////////////
        /// \brief loop all cards
        //////////////////////////////////////////
        for(int i=-1; i<ndigoCount + ndigo250mCount * NDIGO250M_DMA_COUNT; i++) //do the actual work
        {
            volatile ndigo_packet* packet;
            int c = -1; int dma = 0;



            //////////////////////////////////////////
            /// \brief figure out, which packet to read
            //////////////////////////////////////////
            if(i==-1){
                if(readoutErr == 0) packet = out.first_packet; // read trigger card first
                else packet = NULL;
            }
            else if (i==trigcardNo) packet = NULL;
            else if (i < ndigoCount){
                if (ndigo_read(ndigos[i], &in, &out) == 0)  // read the remainig 5G cards
                {
                    packet = out.first_packet;
                    b_nodata=false;
                }
                else packet = NULL;
            }
            else{// read 250M card
                c = qFloor((double)(i-ndigoCount)/(double)NDIGO250M_DMA_COUNT) + ndigoCount; // 250M card number
                dma = i - ndigoCount - (c - ndigoCount) * NDIGO250M_DMA_COUNT; // DMA channel
                //qInfo() << "Attempt to read 250M card. readoutErr = " << readoutErr250m;
                if(dma < NDIGO250M_DMA_COUNT && dma >= 0){
                    if( dma == 0 ) readoutErr250m = ndigo250m_read(ndigos[c], &in250m, &out250m);
                    if(readoutErr250m == 0){
                        if (out250m.error_code[dma] != NDIGO_READ_OK) packet = NULL;
                        else{
                            packet = out250m.first_packet[dma];
                            b_nodata=false;
                        }
                    }
                    else packet = NULL;
                }
                else packet = NULL;
            }

            int n= (i==-1) ? trigcardNo : (( i < ndigoCount ) ? i : c); //this is the actual index of the card presently read out (only ised for ndigo_process_tdc_packet)


            if((!event.validTrigger && !m_sampleNow) || (packet==NULL)) //if nothing is to do here, start over
                continue;

            //////////////////////////////////////////
            /// \brief loop through packet
            //////////////////////////////////////////
            while (packet <= (c == -1 ? out.last_packet : out250m.end[dma]))
            {
                if (packet->type == NDIGO_PACKET_TYPE_16_BIT_SIGNED || packet->type == NDIGO_PACKET_TYPE_TDC_DATA) //ADC or TDC channel
                {
                    if(packet->type == NDIGO_PACKET_TYPE_TDC_DATA) ndigo_process_tdc_packet(ndigos[n], (ndigo_packet*)packet);
                    qint64 relative_time = packet->timestamp - event.timestamp;
                    bool b_trig = (event.validTrigger && relative_time > initpars.main.range_start && relative_time < initpars.main.range_stop);

                    /////////////////////////////////////////////////////////////////
                    /// Prepare trace for user interface
                    ///
                    /// /////////////////////////////////////////////////////////////

                    if ((b_trig || !statuspars.m_bTrigOnly) && packet->card == statuspars.active_card && packet->channel == statuspars.active_chan && m_sampleNow)//plot sample
                    {
                        x.fill(0,0);
                        y.fill(0,0);
                        short* data2 = (short*)packet->data;
                        for (unsigned int j = 0; j < 100 && j < packet->length * 4; j++)
                        {
                            if(n<ndigoCount){
                                x.append(10+j*0.8);
                                y.append(*data2 * 0.00762939453);
                            }
                            else{
                                x.append((10+j*0.8)*4);
                                y.append(*data2 * 0.00762939453 * 2);
                            }
                            data2++;
                        }
                        emit sampleready(x,y);
                        if(m_nofsamples>0) m_nofsamples--;
                        m_sampleNow=false;
                    }

                    /////////////////////////////////////////////////////////////////
                    /// prepare buffer for online analysis (go4)
                    ///
                    /// //////////////////////////////////////////////////////////////

                    if (b_trig && nofClients>0 && nof_signals < GO4_BUFFERSIZE) //fill buffer for go4
                    {
                        if (nof_signals == 0) dataBffr << (qint64)trigger.channel << (qint64)trigger.time;
                        if(packet->type == NDIGO_PACKET_TYPE_16_BIT_SIGNED)
                        {
                            dataBffr << (qint64)(packet->card * 5 + packet->channel);
                            short* data2 = (short*)&packet->data;
                            dataBffr << (qint64)(packet->timestamp +time_CF(data2, packet->length * 4));
                            nof_signals++;
                        }
                        else
                        {
                            dataBffr << (qint64)(packet->card * 5 + packet->channel) << (qint64)(packet->timestamp);
                            nof_signals++;
                        }
                    }

                    ///////////////////////////////////////////////////////////////////////
                    /// Write data to file
                    ///
                    /// ///////////////////////////////////////////////////////////////////
                    if (b_trig && m_writetofile) //write data to file
                    {
                        int size = 4 * sizeof(unsigned char) + sizeof(unsigned int) + sizeof(qint64); // size of packet header
                        size += packet->length * sizeof(quint64); // size of data in packet

                        if(fileOut.writeRawData((char *)packet, size) == -1)
                        {
                            emit errormessage("cannot write to file, close it");
                            stopWritingtoFile();
                        }


                        filesize += size;
                    }
                }
                //ndigo_acknowledge(ndigos[n], packet);
                volatile ndigo_packet* next_packet = ndigo_next_packet(packet);
                packet = next_packet;
            }
        }


        ////////////////////////////////////////////////////////
        /// send bufferto go4
        /// ////////////////////////////////////////////////////
        if(event.validTrigger && nof_signals > 0)
        {
            //go4buffer.resize(nof_signals*sizeof(bufferEvnt));
            emit go4event(nof_signals, go4buffer);
        }

        ////////////////////////////////////////////////////////
        /// write end of buffer packet to file
        /// ////////////////////////////////////////////////////
        if(event.validTrigger && m_writetofile)
        {
            int size = 4 * sizeof(unsigned char) + sizeof(unsigned int) + sizeof(qint64); // size of packet header
            ndigo_packet eob;
            eob.flags =	NDIGO_PACKET_TYPE_END_OF_BUFFER;
            eob.timestamp = event.timestamp;
            if(fileOut.writeRawData((char *)&eob, size) == -1)
            {
                emit errormessage("cannot write to file, close it");
                stopWritingtoFile();
            }
        }


        if (b_nodata) QThread::msleep(5); //No data... try again in 5msec
        QCoreApplication::processEvents();
    }



    for (int m = 0; m < ndigoCount + ndigo250mCount; m++) ndigo_stop_capture(ndigos[m]);

    if(m_restart){
        emit startloop();
    }

    m_count=0;
    m_restart=false;
    emit logmessage("ACQ loop stopped.");
}

void acqcontrol::restart()
{
    qInfo() << this << "restart button pressed";
    if(!m_isrunning)
    {
        emit startloop();
        return;
    }
    m_isrunning=false;
    m_restart=true;
}

void acqcontrol::quit()
{
    qInfo() << this << "stop button pressed";
    m_isrunning=false;
    m_restart=false;
}

void acqcontrol::writeDatatoFile(QFile *dataFile)
{
    if(m_writetofile)
    {
        emit errormessage("there is an open file already.");
        return;
    }
    sFile = dataFile;
    m_writetofile = true;
    filesize=0;
}

void acqcontrol::stopWritingtoFile()
{
    if(!m_writetofile) return;
    m_writetofile = false;
    emit closeDataFile();
}

void acqcontrol::PlotTrigger()
{
    if(m_isrunning && m_nofsamples!=0)
    {
        m_sampleNow=true;
        //emit sampleready(x,y);
        //if(m_nofsamples>0) m_nofsamples--;
        //qInfo() << this << "test";
    }
}

void acqcontrol::RateTrigger()
{
    //qInfo() << this << m_count;

    if(m_isrunning)
    {
        emit rate(m_count);
        m_count=0;
    }
    //timerRate->start(1000);
}

int acqcontrol::initCards()
{
    ndigo_init_parameters init_params[NR_CARDS];
    int error_code;
    const char *error_message;
    QString logmsg;

    //Set up initialization struct with board 0 as master
    //Find out how many Ndigo5G boards are present
    ndigoCount = ndigo_count_devices(&error_code, &error_message);
    if (error_message != QString("OK")) {
        emit errormessage(tr("Initialization error on 5G cards!!!"));
        return 1;
    }
    logmsg = tr("ndigo_count_devices returns: %1").arg(error_message) + " ... ";
    logmsg = logmsg + tr("the number of 5G boards is: %1").arg(ndigoCount);
    emit logmessage(logmsg);


    //Find out how many Ndigo250M  boards are present
    ndigo250mCount = ndigo250m_count_devices(&error_code, &error_message);
    if (error_message != QString("OK")) {
        emit errormessage(tr("Initialization error on 250M cards!!!"));
        return 1;
    }
    logmsg = tr("ndigo250m_count_devices returns: %1").arg(error_message) + " ... ";
    logmsg = logmsg + tr("the number of 250M boards is: %1").arg(ndigo250mCount);
    emit logmessage(logmsg);


    ////////////////////////////////////////////
    /// \brief initialize 5G cards
    ////////////////////////////////////////////
    ndigo_device* ndigo_temp[NR_CARDS - NR_250MCARDS];
    for (int i = 0; i < ndigoCount && i < NR_CARDS - NR_250MCARDS; i++) {
        ndigo_get_default_init_parameters(&init_params[i]);
        init_params[i].card_index = i;
        init_params[i].version = NDIGO_API_VERSION;
        init_params[i].hptdc_sync_enabled = 0;
        init_params[i].board_id = i;
        init_params[i].drive_external_clock = (i == 0);
        init_params[i].use_external_clock = (i != 0);
        init_params[i].is_slave = (i != 0);// ((initpars.main.sourcecard < ndigoCount) ? initpars.main.sourcecard : 0));
        init_params[i].sync_period = 4;
        init_params[i].multiboard_sync = 1;
        init_params[i].buffer_size[0] = BUFFER_SIZE_5G;

        ndigo_temp[i] = ndigo_init(&init_params[i], &error_code, &error_message);
        qInfo() << error_code << error_message;

        if (error_code)
        {
            logmsg = tr("Error %1 during ADC (5G) - init: %2 ...").arg(error_code).arg(error_message);
            emit errormessage(logmsg);

            return 1;
        }
        ndigo_get_static_info(ndigo_temp[i], &ndigo_info[i]);
    }

    //sort 5G cards according to their board serials
    for (int i = 0; i < ndigoCount  && i < NR_CARDS; i++)
    {
        int serial = ndigo_info[i].board_serial;
        int device_id = 0;

        for (int k = 0; k < ndigoCount; k++)
        {
            if (serial < ndigo_info[k].board_serial) device_id++;

        }
        qInfo() << "ndigo serial: " << serial << ", Device ID: " << device_id;
        ndigo_set_board_id(ndigo_temp[i], device_id);
        ndigos[device_id] = ndigo_temp[i];
    }


    ////////////////////////////////////////////
    /// \brief initialize 250M card(s)
    ////////////////////////////////////////////
    for (int i = ndigoCount; i < (ndigoCount + ndigo250mCount) && i < NR_CARDS; i++) {
        ndigo250m_get_default_init_parameters(&init_params[i]);
        init_params[i].card_index = 0;
        for (int j = 0; j < NDIGO250M_DMA_COUNT; j++) init_params[i].buffer_size[j] = BUFFER_SIZE_250M;
        init_params[i].hptdc_sync_enabled = 0;
        init_params[i].board_id = i;
        init_params[i].drive_external_clock = 0;
        init_params[i].use_external_clock = 1;
        init_params[i].is_slave = 1;
        init_params[i].sync_period = 4;
        init_params[i].multiboard_sync = 1;
        ndigos[i] = ndigo250m_init(&init_params[i], &error_code, &error_message);
        qInfo() << "250M board initialized: " << error_code << error_message;
        if (error_code)
        {
            logmsg = tr("Error %1 during ADC (250M) - init: %2 ...").arg(error_code).arg(error_message);
            emit errormessage(logmsg);

            return 1;
        }
    }

    ///////////////////////////////////////////////////////////////////////////////
    // read general info from the ADC-card (serial number, firmware version, ...):
    for (int i = 0; i < ndigoCount + ndigo250mCount; i++){
        ndigo_get_static_info(ndigos[i], &ndigo_info[i]);
        qInfo() << "serial: " << ndigo_info[i].board_serial << ", board id:" << ndigo_info[i].board_id;
    }

    ///////////////////////////////////////////////////////////////////////////////


    m_isinitialized=true;

    return 0;
}


int acqcontrol::initAcq()
{
    QString logmsg;


    if(!m_isinitialized) return 1;

    ////////////////////////////////////////////////////
    /// \brief configure 5G cards (zero supression etc.)
    ////////////////////////////////////////////////////
    for (int i = 0; i < ndigoCount; i++) {
        int status = ndigo_get_default_configuration(ndigos[i], &conf[i]);
        if(status)
        {
            emit errormessage(tr("Couldn't get default configuration from ndigo board. Break!"));
            return 1;
        }

        conf[i].adc_mode = NDIGO_ADC_MODE_ABCD; // 1.25Gs/s, 4 channels

        conf[i].trigger[NDIGO_TRIGGER_A0].threshold = (short)(initpars.card[i].chan[0].thresh * 131);
        conf[i].trigger[NDIGO_TRIGGER_A0].edge = initpars.card[i].chan[0].edge;
        conf[i].trigger[NDIGO_TRIGGER_A0].rising = initpars.card[i].chan[0].rising;

        conf[i].trigger[NDIGO_TRIGGER_B0].threshold = (short)(initpars.card[i].chan[1].thresh * 131);
        conf[i].trigger[NDIGO_TRIGGER_B0].edge = initpars.card[i].chan[1].edge;
        conf[i].trigger[NDIGO_TRIGGER_B0].rising = initpars.card[i].chan[1].rising;

        conf[i].trigger[NDIGO_TRIGGER_C0].threshold = (short)(initpars.card[i].chan[2].thresh * 131);
        conf[i].trigger[NDIGO_TRIGGER_C0].edge = initpars.card[i].chan[2].edge;
        conf[i].trigger[NDIGO_TRIGGER_C0].rising = initpars.card[i].chan[2].rising;

        conf[i].trigger[NDIGO_TRIGGER_D0].threshold = (short)(initpars.card[i].chan[3].thresh * 131);
        conf[i].trigger[NDIGO_TRIGGER_D0].edge = initpars.card[i].chan[3].edge;
        conf[i].trigger[NDIGO_TRIGGER_D0].rising = initpars.card[i].chan[3].rising;

        conf[i].trigger[NDIGO_TRIGGER_TDC].edge = initpars.card[i].chan[4].edge; // edge trigger
        conf[i].trigger[NDIGO_TRIGGER_TDC].rising = initpars.card[i].chan[4].rising; // rising edge


        if (i == 0) {
            //die folgende Zeilen wurden von Till Jahnke empfohlen (MW)
            conf[i].drive_bus[0] = NDIGO_TRIGGER_SOURCE_A0; //puts ion MCP signal on BUS0 such that all cards have access to it (MW)
            conf[i].drive_bus[1] = NDIGO_TRIGGER_SOURCE_B0; //puts electron MCP signal on BUS1 such that all cards have access to it (MW)
            conf[i].drive_bus[2] = NDIGO_TRIGGER_SOURCE_TDC; //puts projectile pulse signal on BUS2 such that all cards have access to it (MW)
        }
        conf[i].trigger_block[0].gates = NDIGO_TRIGGER_GATE_NONE; // hardware gate
        conf[i].trigger_block[1].gates = NDIGO_TRIGGER_GATE_NONE; // no hardware gate
        conf[i].trigger_block[2].gates = NDIGO_TRIGGER_GATE_NONE; // no hardware gate
        conf[i].trigger_block[3].gates = NDIGO_TRIGGER_GATE_NONE; // no hardware gate

        conf[i].trigger_block[0].sources = NDIGO_TRIGGER_SOURCE_A0; // trigger source is channel A
        conf[i].trigger_block[1].sources = NDIGO_TRIGGER_SOURCE_B0; // trigger source is channel A
        conf[i].trigger_block[2].sources = NDIGO_TRIGGER_SOURCE_C0; // trigger source is channel A
        conf[i].trigger_block[3].sources = NDIGO_TRIGGER_SOURCE_D0; // trigger source is channel A

        for (int k = 0; k < 4; k++) {

            //		conf[i].trigger_block[k].sources = pow((float) 2, 2*k); // trigger source is channel A
            //		conf[i].trigger_block[k].gates = NDIGO_TRIGGER_GATE_0;
            conf[i].trigger_block[k].precursor = initpars.card[i].cardPars.precursor; // precursor in in steps of 3.2ns.
            conf[i].trigger_block[k].length = initpars.card[i].cardPars.length; // amount of samples to record after the trigger in 3.2ns steps
            conf[i].trigger_block[k].retrigger = initpars.card[i].cardPars.retrigger;
            conf[i].trigger_block[k].enabled = true; // enable trigger
            conf[i].trigger_block[k].minimum_free_packets = 1.0; //

            conf[i].analog_offset[k] = 0; // position of the baseline. 0 is gnd. Range is ~ -0.22 bis +0.22. Correspnding to ~+-0.5V
        }
        conf[i].dc_offset[0] = 0.4;
        conf[i].tdc_enabled = true; // TDC
    }



    ////////////////////////////////////////////////////////
    /// \brief configure 250M card(s) (zero supression etc.)
    ////////////////////////////////////////////////////////
    for (int i = ndigoCount; i < ndigoCount + ndigo250mCount && i < NR_CARDS; i++) {
        int ii = i-ndigoCount;

        int status = ndigo250m_get_default_configuration(ndigos[i], &conf250m[ii]);
        if(status)
        {
            emit errormessage(tr("Couldn't get default configuration from ndigo board. Break!"));
            return 1;
        }
        for (int j = 0; j < 6; j++) conf250m[ii].analog_offset[j]=0;

        conf250m[ii].trigger[NDIGO_TRIGGER_A0].edge = initpars.card[i].chan[0].edge;
        conf250m[ii].trigger[NDIGO_TRIGGER_A0].rising = initpars.card[i].chan[0].rising;;
        conf250m[ii].trigger[NDIGO_TRIGGER_A0].threshold = (short)(initpars.card[i].chan[0].thresh * 262);

        conf250m[ii].trigger[NDIGO_TRIGGER_B0].edge = initpars.card[i].chan[1].edge;
        conf250m[ii].trigger[NDIGO_TRIGGER_B0].rising = initpars.card[i].chan[1].rising;;
        conf250m[ii].trigger[NDIGO_TRIGGER_B0].threshold = (short)(initpars.card[i].chan[1].thresh * 262);

        conf250m[ii].trigger[NDIGO_TRIGGER_C0].edge = initpars.card[i].chan[2].edge;
        conf250m[ii].trigger[NDIGO_TRIGGER_C0].rising = initpars.card[i].chan[2].rising;;
        conf250m[ii].trigger[NDIGO_TRIGGER_C0].threshold = (short)(initpars.card[i].chan[2].thresh * 262);

        conf250m[ii].trigger[NDIGO_TRIGGER_D0].edge = initpars.card[i].chan[3].edge;
        conf250m[ii].trigger[NDIGO_TRIGGER_D0].rising = initpars.card[i].chan[3].rising;
        conf250m[ii].trigger[NDIGO_TRIGGER_D0].threshold = (short)(initpars.card[i].chan[3].thresh * 262);

        conf250m[ii].trigger[NDIGO_TRIGGER_TDC].edge = initpars.card[i].chan[4].edge; // edge trigger
        conf250m[ii].trigger[NDIGO_TRIGGER_TDC].rising = initpars.card[i].chan[4].rising; // rising edge


        conf250m[ii].trigger_block[0].gates = NDIGO_TRIGGER_GATE_NONE; // hardware gate
        conf250m[ii].trigger_block[1].gates = NDIGO_TRIGGER_GATE_NONE; // no hardware gate
        conf250m[ii].trigger_block[2].gates = NDIGO_TRIGGER_GATE_NONE; // no hardware gate
        conf250m[ii].trigger_block[3].gates = NDIGO_TRIGGER_GATE_NONE; // no hardware gate

        conf250m[ii].trigger_block[0].sources = NDIGO_TRIGGER_SOURCE_B0;
        conf250m[ii].trigger_block[1].sources = NDIGO_TRIGGER_SOURCE_B0;
        conf250m[ii].trigger_block[2].sources = NDIGO_TRIGGER_SOURCE_B0;
        conf250m[ii].trigger_block[3].sources = NDIGO_TRIGGER_SOURCE_B0;


        for (int k = 0; k < 4; k++) {
            conf250m[ii].trigger_block[k].precursor = initpars.card[i].cardPars.precursor; // precursor in in steps of 4ns.
            conf250m[ii].trigger_block[k].length = initpars.card[i].cardPars.length; // amount of samples to record after the trigger in 4ns steps
            conf250m[ii].trigger_block[k].retrigger = initpars.card[i].cardPars.retrigger;
            conf250m[ii].trigger_block[k].enabled = true; // enable trigger
            conf250m[ii].trigger_block[k].minimum_free_packets = 1.0; //
        }
    }


    if (initpars.main.advconf) ReadAdvConfig();


    //Submit configuration
    for (int i = 0; i < ndigoCount; i++) {
        if(ndigo_configure(ndigos[i], &conf[i]) != NDIGO_OK) {// write configuration to board
            logmsg = tr("5G board configuration failed. Abort! ");
            emit errormessage(logmsg);
            return 1;
        }
        else qInfo() << "Configuration submitted to board " << i;
    }

    for (int i = ndigoCount; i < ndigoCount + ndigo250mCount && i < NR_CARDS; i++) {
        int ii = i-ndigoCount;
        if(ndigo250m_configure(ndigos[i], &conf250m[ii]) != NDIGO_OK) {// write configuration to board
            logmsg = tr("250M board configuration failed. Abort! ");
            emit errormessage(logmsg);
            return 1;
        }
        else qInfo() << "Configuration submitted to board " << i;
    }


    ndigo_fast_info fastinfo;
    if(ndigoCount > 0) ndigo_get_fast_info(ndigos[0], &fastinfo);
    logmsg = tr(" boards successfully configured ... ");
    emit logmessage(logmsg);
    //m_bIsconfigured = true;

    return 0;
}

void acqcontrol::ReadAdvConfig()
{
    QFile inputFile(initpars.main.advfilename);
    if(!inputFile.open(QIODevice::ReadOnly | QIODeviceBase::Text))
    {
        QString errmessage = tr("Couldn't open file %1").arg(initpars.main.advfilename);
        emit errormessage(errmessage);
        return;
    }
    QString message = tr("File %1 opened:").arg(initpars.main.advfilename);
    emit logmessage(message);


    QTextStream in(&inputFile);
    while (!in.atEnd())
    {
        QString line = in.readLine();

        //skip comments and empty lines
        if(line==QString("") || line.first(1)==QString("#") || line.first(2)==QLatin1StringView("//")) continue;

        //remove everything after ';'
        line = line.mid(0, line.indexOf(";"));

        //replace common cronologic constants
        line.replace(QLatin1String("NDIGO_ADC_MODE_ABCD"), QLatin1String("0"));
        line.replace(QLatin1String("NDIGO_ADC_MODE_AC"), QLatin1String("4"));
        line.replace(QLatin1String("NDIGO_ADC_MODE_BC"), QLatin1String("5"));
        line.replace(QLatin1String("NDIGO_ADC_MODE_AD"), QLatin1String("6"));
        line.replace(QLatin1String("NDIGO_ADC_MODE_BD"), QLatin1String("7"));
        line.replace(QLatin1String("NDIGO_ADC_MODE_A"), QLatin1String("8"));
        line.replace(QLatin1String("NDIGO_ADC_MODE_B"), QLatin1String("9"));
        line.replace(QLatin1String("NDIGO_ADC_MODE_C"), QLatin1String("10"));
        line.replace(QLatin1String("NDIGO_ADC_MODE_D"), QLatin1String("11"));
        line.replace(QLatin1String("NDIGO_ADC_MODE_AAAA"), QLatin1String("12"));
        line.replace(QLatin1String("NDIGO_ADC_MODE_BBBB"), QLatin1String("13"));
        line.replace(QLatin1String("NDIGO_ADC_MODE_CCCC"), QLatin1String("14"));
        line.replace(QLatin1String("NDIGO_ADC_MODE_DDDD"), QLatin1String("15"));
        line.replace(QLatin1String("NDIGO_ADC_MODE_A12"), QLatin1String("28"));
        line.replace(QLatin1String("NDIGO_ADC_MODE_B12"), QLatin1String("29"));
        line.replace(QLatin1String("NDIGO_ADC_MODE_C12"), QLatin1String("30"));
        line.replace(QLatin1String("NDIGO_ADC_MODE_D12"), QLatin1String("31"));
        line.replace(QLatin1String("NDIGO_MAX_PRECURSOR"), QLatin1String("25"));
        line.replace(QLatin1String("NDIGO_MAX_MULTISHOT"), QLatin1String("65535"));
        line.replace(QLatin1String("NDIGO_FIFO_DEPTH"), QLatin1String("8176"));
        line.replace(QLatin1String("NDIGO_OUTPUT_MODE_SIGNED16"), QLatin1String("0"));
        line.replace(QLatin1String("NDIGO_OUTPUT_MODE_RAW"), QLatin1String("1"));
        line.replace(QLatin1String("NDIGO_OUTPUT_MODE_CUSTOM"), QLatin1String("2"));
        line.replace(QLatin1String("NDIGO_OUTPUT_MODE_CUSTOM_INL"), QLatin1String("3"));

        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_NONE"), QLatin1String("0x00000000"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_A0"), QLatin1String("0x00000001"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_A1"), QLatin1String("0x00000002"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_B0"), QLatin1String("0x00000004"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_B1"), QLatin1String("0x00000008"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_C0"), QLatin1String("0x00000010"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_C1"), QLatin1String("0x00000020"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_D0"), QLatin1String("0x00000040"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_D1"), QLatin1String("0x00000080"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_TDC"), QLatin1String("0x00000100"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_GATE"), QLatin1String("0x00000200"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_BUS0"), QLatin1String("0x00000400"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_BUS1"), QLatin1String("0x00000800"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_BUS2"), QLatin1String("0x00001000"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_BUS3"), QLatin1String("0x00002000"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_AUTO"), QLatin1String("0x00004000"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_ONE"), QLatin1String("0x00008000"));

        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_TDC_PE"), QLatin1String("0x01000000"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_GATE_PE"), QLatin1String("0x02000000"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_BUS0_PE"), QLatin1String("0x04000000"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_BUS1_PE"), QLatin1String("0x08000000"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_BUS2_PE"), QLatin1String("0x10000000"));
        line.replace(QLatin1String("NDIGO_TRIGGER_SOURCE_BUS3_PE"), QLatin1String("0x20000000"));

        line.replace(QLatin1String("NDIGO_TRIGGER_GATE_NONE"), QLatin1String("0x0000"));
        line.replace(QLatin1String("NDIGO_TRIGGER_GATE_0"), QLatin1String("0x0001"));
        line.replace(QLatin1String("NDIGO_TRIGGER_GATE_1"), QLatin1String("0x0002"));
        line.replace(QLatin1String("NDIGO_TRIGGER_GATE_2"), QLatin1String("0x0004"));
        line.replace(QLatin1String("NDIGO_TRIGGER_GATE_3"), QLatin1String("0x0008"));

        line.replace(QLatin1String("NDIGO_BUFFER_ALLOCATE"), QLatin1String("0"));
        line.replace(QLatin1String("NDIGO_BUFFER_USE_PHYSICAL"), QLatin1String("1"));
        line.replace(QLatin1String("NDIGO_BUFFER_USE_PREALLOCATED"), QLatin1String("2"));

        line.replace(QLatin1String("NDIGO_TRIGGER_A0"), QLatin1String("0"));
        line.replace(QLatin1String("NDIGO_TRIGGER_A1"), QLatin1String("1"));
        line.replace(QLatin1String("NDIGO_TRIGGER_B0"), QLatin1String("2"));
        line.replace(QLatin1String("NDIGO_TRIGGER_B1"), QLatin1String("3"));
        line.replace(QLatin1String("NDIGO_TRIGGER_C0"), QLatin1String("4"));
        line.replace(QLatin1String("NDIGO_TRIGGER_C1"), QLatin1String("5"));
        line.replace(QLatin1String("NDIGO_TRIGGER_D0"), QLatin1String("6"));
        line.replace(QLatin1String("NDIGO_TRIGGER_D1"), QLatin1String("7"));
        line.replace(QLatin1String("NDIGO_TRIGGER_TDC"), QLatin1String("8"));
        line.replace(QLatin1String("NDIGO_TRIGGER_GATE"), QLatin1String("9"));
        line.replace(QLatin1String("NDIGO_TRIGGER_BUS0"), QLatin1String("10"));
        line.replace(QLatin1String("NDIGO_TRIGGER_BUS1"), QLatin1String("11"));
        line.replace(QLatin1String("NDIGO_TRIGGER_BUS2"), QLatin1String("12"));
        line.replace(QLatin1String("NDIGO_TRIGGER_BUS3"), QLatin1String("13"));
        line.replace(QLatin1String("NDIGO_TRIGGER_AUTO"), QLatin1String("14"));
        line.replace(QLatin1String("NDIGO_TRIGGER_ONE"), QLatin1String("15"));

        line.replace(QLatin1String("NDIGO_TRIGGER_TDC_PE"), QLatin1String("16"));

        line.replace(QLatin1String("NDIGO_TRIGGER_GATE_PE"), QLatin1String("17"));
        line.replace(QLatin1String("NDIGO_TRIGGER_BUS0_PE"), QLatin1String("18"));
        line.replace(QLatin1String("NDIGO_TRIGGER_BUS1_PE"), QLatin1String("19"));
        line.replace(QLatin1String("NDIGO_TRIGGER_BUS2_PE"), QLatin1String("20"));
        line.replace(QLatin1String("NDIGO_TRIGGER_BUS3_PE"), QLatin1String("21"));


        line.replace(QLatin1String("NDIGO250M_MAX_PRECURSOR"), QLatin1String("27"));
        line.replace(QLatin1String("NDIGO250M_MAX_MULTISHOT"), QLatin1String("65535"));
        line.replace(QLatin1String("NDIGO250M_FIFO_DEPTH"), QLatin1String("4094"));
        line.replace(QLatin1String("NDIGO250M_CHANNEL_A_MASK"), QLatin1String("1"));
        line.replace(QLatin1String("NDIGO250M_CHANNEL_B_MASK"), QLatin1String("2"));
        line.replace(QLatin1String("NDIGO250M_CHANNEL_C_MASK"), QLatin1String("4"));
        line.replace(QLatin1String("NDIGO250M_CHANNEL_D_MASK"), QLatin1String("8"));
        line.replace(QLatin1String("NDIGO250M_CHANNEL_T_MASK"), QLatin1String("16"));
        line.replace(QLatin1String("NDIGO250M_CHANNEL_COUNT"), QLatin1String("4"));
        line.replace(QLatin1String("NDIGO250M_TDC_COUNT"), QLatin1String("1"));
        line.replace(QLatin1String("NDIGO250M_DMA_COUNT"), QLatin1String("6"));
        line.replace(QLatin1String("NDIGO250M_GATE_COUNT"), QLatin1String("4"));


        line.replace(QLatin1String("true"), QLatin1String("1"),Qt::CaseInsensitive);
        line.replace(QLatin1String("false"), QLatin1String("0"),Qt::CaseInsensitive);


        //separate equation in left-handed side (LHS) and right-handed side (RHS)
        QStringList equ = line.split(u'=');
        bool ok;
        if(equ.size()!=2)
        {
            emit errormessage(QLatin1String("Couldn't interpret %1").arg(line));
            continue;
        }
        QString lhs = equ.at(0).trimmed();
        QString rhs = equ.at(1).trimmed();

        //extract indices on LHS
        static QRegularExpression re("\\d+");
        QRegularExpressionMatchIterator i = re.globalMatch(lhs);
        int nindex=0, index[10]={0};
        while (i.hasNext() && nindex<10) {
            QRegularExpressionMatch match = i.next();
            index[nindex] = match.captured(0).toInt(&ok);
            if(!ok)
            {
                emit errormessage(QLatin1String("Couldn't interpret %1").arg(line));
                continue;
            }
            nindex++;
        }


        //split LHS into different components
        QStringList varList = lhs.split(u'.');

        /////////////////////////////////////////////////////////////////
        /// \brief read 5G configuration entries "... conf[i].... = value"
        /////////////////////////////////////////////////////////////////
        if(nindex>0 && (varList.at(0).compare(QString("conf[%1]").arg(index[0]),Qt::CaseInsensitive)==0) && index[0]>=0 && index[0]<ndigoCount)
        {
            if(varList.size()==2)
            {
                if (varList.at(1).compare(QLatin1String("size"),Qt::CaseInsensitive)==0) {
                    int value = rhs.toInt(&ok,0);
                    if(ok && value>=0)
                    {
                        conf[index[0]].size = value;
                        emit logmessage(QString("conf[%1].size = %2").arg(index[0]).arg(value));
                        continue;
                    }
                }
                else if (varList.at(1).compare(QLatin1String("version"),Qt::CaseInsensitive)==0) {
                    int value = rhs.toInt(&ok,0);
                    if(ok)
                    {
                        conf[index[0]].version = value;
                        emit logmessage(QString("conf[%1].version = %2").arg(index[0]).arg(value));
                        continue;
                    }
                }
                else if (varList.at(1).compare(QLatin1String("adc_mode"),Qt::CaseInsensitive)==0) {
                    int value = rhs.toInt(&ok,0);
                    if(ok)
                    {
                        conf[index[0]].adc_mode = value;
                        emit logmessage(QString("conf[%1].adc_mode = %2").arg(index[0]).arg(value));
                        continue;
                    }
                }
                else if (varList.at(1).compare(QLatin1String("bandwidth"),Qt::CaseInsensitive)==0) {
                    double value = rhs.toDouble(&ok);
                    if(ok)
                    {
                        conf[index[0]].bandwidth = value;
                        emit logmessage(QString("conf[%1].bandwidth = %2").arg(index[0]).arg(value));
                        continue;
                    }
                }
                else if (varList.at(1).compare(QLatin1String("tdc_enabled"),Qt::CaseInsensitive)==0) {
                    int value = rhs.toInt(&ok,0);
                    if(ok)
                    {
                        conf[index[0]].tdc_enabled = (value == 0 ? false : true);
                        emit logmessage(QString("conf[%1].tdc_enabled = %2").arg(index[0]).arg(value));
                        continue;
                    }
                }
                else if (varList.at(1).compare(QLatin1String("tdc_fb_enabled"),Qt::CaseInsensitive)==0) {
                    int value = rhs.toInt(&ok,0);
                    if(ok)
                    {
                        conf[index[0]].tdc_fb_enabled = (value == 0 ? false : true);
                        emit logmessage(QString("conf[%1].tdc_fb_enabled = %2").arg(index[0]).arg(value));
                        continue;
                    }
                }
                else if (varList.at(1).compare(QLatin1String("auto_trigger_period"),Qt::CaseInsensitive)==0) {
                    int value = rhs.toInt(&ok,0);
                    if(ok)
                    {
                        conf[index[0]].auto_trigger_period = value;
                        emit logmessage(QString("conf[%1].auto_trigger_period = %2").arg(index[0]).arg(value));
                        continue;
                    }
                }
                else if (varList.at(1).compare(QLatin1String("auto_trigger_random_exponent"),Qt::CaseInsensitive)==0) {
                    int value = rhs.toInt(&ok,0);
                    if(ok)
                    {
                        conf[index[0]].auto_trigger_random_exponent = value;
                        emit logmessage(QString("conf[%1].auto_trigger_random_exponent = %2").arg(index[0]).arg(value));
                        continue;
                    }
                }
                else if (varList.at(1).compare(QLatin1String("output_mode"),Qt::CaseInsensitive)==0) {
                    int value = rhs.toInt(&ok,0);
                    if(ok)
                    {
                        conf[index[0]].output_mode = value;
                        emit logmessage(QString("conf[%1].output_mode = %2").arg(index[0]).arg(value));
                        continue;
                    }
                }
                else if(nindex==2)
                {
                    if (varList.at(1).compare(QString("analog_offset[%1]").arg(index[1]),Qt::CaseInsensitive)==0) {
                        double value = rhs.toDouble(&ok);
                        if (index[1] >= 0 && index[1] < NDIGO_CHANNEL_COUNT && ok) {
                            conf[index[0]].analog_offset[index[1]] = value;
                            emit logmessage(QString("conf[%1].analog_offset[%2] = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    else if (varList.at(1).compare(QString("dc_offset[%1]").arg(index[1]),Qt::CaseInsensitive)==0) {
                        double value = rhs.toDouble(&ok);
                        if (index[1] >= 0 && index[1] < 2 && ok) {
                            conf[index[0]].dc_offset[index[1]] = value;
                            emit logmessage(QString("conf[%1].dc_offset[%2] = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    else if (varList.at(1).compare(QString("drive_bus[%1]").arg(index[1]),Qt::CaseInsensitive)==0) {
                        int value = rhs.toInt(&ok,0);
                        if (index[1] >= 0 && index[1] < 4 && ok) {
                            conf[index[0]].drive_bus[index[1]] = value;
                            emit logmessage(QString("conf[%1].drive_bus[%2] = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                }
            }
            else if(varList.size()==3 && nindex==2)
            {
                if (varList.at(1).compare(QString("trigger[%1]").arg(index[1]),Qt::CaseInsensitive) == 0 && index[1] >= 0 && index[1] < NDIGO_TRIGGER_COUNT + NDIGO_ADD_TRIGGER_COUNT)
                {
                    if (varList.at(2).compare(QString("threshold"), Qt::CaseInsensitive) == 0) {
                        int value = rhs.toInt(&ok,0)*131;
                        if (value >= -32768 && value <= 32768 && ok) {
                            conf[index[0]].trigger[index[1]].threshold = (short)value;
                            emit logmessage(QString("conf[%1].trigger[%2].threshold = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("edge"), Qt::CaseInsensitive) == 0) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf[index[0]].trigger[index[1]].edge = (value == 0 ? false : true);
                            emit logmessage(QString("conf[%1].trigger[%2].edge = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("rising"), Qt::CaseInsensitive) == 0) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf[index[0]].trigger[index[1]].rising = (value == 0 ? false : true);
                            emit logmessage(QString("conf[%1].trigger[%2].rising = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                }
                else if (varList.at(1).compare(QString("trigger_block[%1]").arg(index[1]),Qt::CaseInsensitive) == 0  && index[1] >= 0 && index[1] < NDIGO_CHANNEL_COUNT + 1)
                {
                    if (varList.at(2).compare(QString("enabled"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf[index[0]].trigger_block[index[1]].enabled = (value == 0 ? false : true);
                            emit logmessage(QString("conf[%1].trigger_block[%2].enabled = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("force_led"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf[index[0]].trigger_block[index[1]].force_led = (value == 0 ? false : true);
                            emit logmessage(QString("conf[%1].trigger_block[%2].force_led = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("retrigger"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf[index[0]].trigger_block[index[1]].retrigger = (value == 0 ? false : true);
                            emit logmessage(QString("conf[%1].trigger_block[%2].retrigger = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("multi_shot_count"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf[index[0]].trigger_block[index[1]].multi_shot_count = value;
                            emit logmessage(QString("conf[%1].trigger_block[%2].multi_shot_count = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("precursor"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf[index[0]].trigger_block[index[1]].precursor = value;
                            emit logmessage(QString("conf[%1].trigger_block[%2].precursor = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("length"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf[index[0]].trigger_block[index[1]].length = value;
                            emit logmessage(QString("conf[%1].trigger_block[%2].length = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("sources"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf[index[0]].trigger_block[index[1]].sources = value;
                            emit logmessage(QString("conf[%1].trigger_block[%2].sources = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("gates"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf[index[0]].trigger_block[index[1]].gates = value;
                            emit logmessage(QString("conf[%1].trigger_block[%2].gates = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("minimum_free_packets"),Qt::CaseInsensitive) ==  0 ) {
                        double value = rhs.toDouble(&ok);
                        if(ok)
                        {
                            conf[index[0]].trigger_block[index[1]].minimum_free_packets = value;
                            emit logmessage(QString("conf[%1].trigger_block[%2].minimum_free_packets = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                }
                else if (varList.at(1).compare(QString("gating_block[%1]").arg(index[1]),Qt::CaseInsensitive) == 0 && index[1] >= 0 && index[1] < NDIGO_GATE_COUNT)
                {
                    if (varList.at(2).compare(QString("negate"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf[index[0]].gating_block[index[1]].negate = (value == 0 ? false : true);
                            emit logmessage(QString("conf[%1].gating_block[%2].negate = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("retrigger"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf[index[0]].gating_block[index[1]].retrigger = (value == 0 ? false : true);
                            emit logmessage(QString("conf[%1].gating_block[%2].retrigger = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("extend"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf[index[0]].gating_block[index[1]].extend = (value == 0 ? false : true);
                            emit logmessage(QString("conf[%1].gating_block[%2].extend = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("start"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf[index[0]].gating_block[index[1]].start = value;
                            emit logmessage(QString("conf[%1].gating_block[%2].start = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("stop"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf[index[0]].gating_block[index[1]].stop = value;
                            emit logmessage(QString("conf[%1].gating_block[%2].stop = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("sources"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf[index[0]].gating_block[index[1]].sources = value;
                            emit logmessage(QString("conf[%1].gating_block[%2].sources = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                }
                else if (varList.at(1).compare(QString("extension_block[%1]").arg(index[1]),Qt::CaseInsensitive) == 0 && index[1] >= 0 && index[1] < NDIGO_EXTENSION_COUNT)
                {
                    if (varList.at(2).compare(QString("enable"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf[index[0]].extension_block[index[1]].enable = (value == 0 ? false : true);
                            emit logmessage(QString("conf[%1].extension_block[%2].enable = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("ignore_cable"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf[index[0]].extension_block[index[1]].ignore_cable = (value == 0 ? false : true);
                            emit logmessage(QString("conf[%1].extension_block[%2].ignore_cable = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                }
            }
        }


        /////////////////////////////////////////////////////////////////
        /// \brief read 250M configuration entries "... conf250m[i].... = value"
        /////////////////////////////////////////////////////////////////
        if(nindex>0 && (varList.at(0).compare(QString("conf250m[%1]").arg(index[0]),Qt::CaseInsensitive)==0) && index[0]>=0 && index[0]<ndigo250mCount)
        {
            if(varList.size()==2)
            {
                if (varList.at(1).compare(QLatin1String("size"),Qt::CaseInsensitive)==0) {
                    int value = rhs.toInt(&ok,0);
                    if(ok && value>=0)
                    {
                        conf250m[index[0]].size = value;
                        emit logmessage(QString("conf250m[%1].size = %2").arg(index[0]).arg(value));
                        continue;
                    }
                }
                else if (varList.at(1).compare(QLatin1String("version"),Qt::CaseInsensitive)==0) {
                    int value = rhs.toInt(&ok,0);
                    if(ok)
                    {
                        conf250m[index[0]].version = value;
                        emit logmessage(QString("conf250m[%1].version = %2").arg(index[0]).arg(value));
                        continue;
                    }
                }
                else if (varList.at(1).compare(QLatin1String("tdc_enabled"),Qt::CaseInsensitive)==0) {
                    int value = rhs.toInt(&ok,0);
                    if(ok)
                    {
                        conf250m[index[0]].tdc_enabled = (value == 0 ? false : true);
                        emit logmessage(QString("conf250m[%1].tdc_enabled = %2").arg(index[0]).arg(value));
                        continue;
                    }
                }
                else if (varList.at(1).compare(QLatin1String("tdc_fb_enabled"),Qt::CaseInsensitive)==0) {
                    int value = rhs.toInt(&ok,0);
                    if(ok)
                    {
                        conf250m[index[0]].tdc_fb_enabled = (value == 0 ? false : true);
                        emit logmessage(QString("conf250m[%1].tdc_fb_enabled = %2").arg(index[0]).arg(value));
                        continue;
                    }
                }
                else if (varList.at(1).compare(QLatin1String("auto_trigger_period"),Qt::CaseInsensitive)==0) {
                    int value = rhs.toInt(&ok,0);
                    if(ok)
                    {
                        conf250m[index[0]].auto_trigger_period = value;
                        emit logmessage(QString("conf250m[%1].auto_trigger_period = %2").arg(index[0]).arg(value));
                        continue;
                    }
                }
                else if (varList.at(1).compare(QLatin1String("auto_trigger_random_exponent"),Qt::CaseInsensitive)==0) {
                    int value = rhs.toInt(&ok,0);
                    if(ok)
                    {
                        conf250m[index[0]].auto_trigger_random_exponent = value;
                        emit logmessage(QString("conf250m[%1].auto_trigger_random_exponent = %2").arg(index[0]).arg(value));
                        continue;
                    }
                }
                else if (varList.at(1).compare(QLatin1String("divisor"),Qt::CaseInsensitive)==0) {
                    int value = rhs.toInt(&ok,0);
                    if(ok && value > 1 && value < 33)
                    {
                        conf250m[index[0]].divisor = value;
                        emit logmessage(QString("conf250m[%1].divisor = %2").arg(index[0]).arg(value));
                        continue;
                    }
                }
                else if (varList.at(1).compare(QLatin1String("decimation"),Qt::CaseInsensitive)==0) {
                    int value = rhs.toInt(&ok,0);
                    if(value > 0 && value < 256 && ok)
                    {
                        conf250m[index[0]].decimation = value;
                        emit logmessage(QString("conf250m[%1].decimation = %2").arg(index[0]).arg(value));
                        continue;
                    }
                }
                else if(nindex==2)
                {
                    if (varList.at(1).compare(QString("analog_offset[%1]").arg(index[1]),Qt::CaseInsensitive)==0) {
                        double value = rhs.toDouble(&ok);
                        if (index[1] >= 0 && index[1] < 6 && ok) {
                            conf250m[index[0]].analog_offset[index[1]] = value;
                            emit logmessage(QString("conf250m[%1].analog_offset[%2] = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    else if (varList.at(1).compare(QString("high_gain[%1]").arg(index[1]),Qt::CaseInsensitive)==0) {
                        double value = rhs.toDouble(&ok);
                        if (index[1] >= 0 && index[1] < 2 && value < 2 && value >=0 && ok) {
                            conf250m[index[0]].high_gain[index[1]] = value;
                            emit logmessage(QString("conf250m[%1].high_gain[%2] = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    else if (varList.at(1).compare(QString("fine_gain[%1]").arg(index[1]),Qt::CaseInsensitive)==0) {
                        double value = rhs.toDouble(&ok);
                        if (index[1] >= 0 && index[1] < 2 && value < 13 && value >=0  && ok) {
                            conf250m[index[0]].fine_gain[index[1]] = value;
                            emit logmessage(QString("conf250m[%1].fine_gain[%2] = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    else if (varList.at(1).compare(QString("gain_correction[%1]").arg(index[1]),Qt::CaseInsensitive)==0) {
                        double value = rhs.toDouble(&ok);
                        if (index[1] >= 0 && index[1] < 2 && value < 11 && value >=0  && ok) {
                            conf250m[index[0]].gain_correction[index[1]] = value;
                            emit logmessage(QString("conf250m[%1].gain_correction[%2] = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    else if (varList.at(1).compare(QString("drive_bus[%1]").arg(index[1]),Qt::CaseInsensitive)==0) {
                        int value = rhs.toInt(&ok,0);
                        if (index[1] >= 0 && index[1] < 4 && ok) {
                            conf250m[index[0]].drive_bus[index[1]] = value;
                            emit logmessage(QString("conf250m[%1].drive_bus[%2] = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                }
            }
            else if(varList.size()==3 && nindex==2)
            {
                if (varList.at(1).compare(QString("trigger[%1]").arg(index[1]),Qt::CaseInsensitive) == 0 && index[1] >= 0 && index[1] < NDIGO_TRIGGER_COUNT)
                {
                    if (varList.at(2).compare(QString("threshold"), Qt::CaseInsensitive) == 0) {
                        int value = rhs.toInt(&ok,0)*131;
                        if (value >= -32768 && value <= 32768 && ok) {
                            conf250m[index[0]].trigger[index[1]].threshold = (short)value;
                            emit logmessage(QString("conf250m[%1].trigger[%2].threshold = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("edge"), Qt::CaseInsensitive) == 0) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf250m[index[0]].trigger[index[1]].edge = (value == 0 ? false : true);
                            emit logmessage(QString("conf250m[%1].trigger[%2].edge = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("rising"), Qt::CaseInsensitive) == 0) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf250m[index[0]].trigger[index[1]].rising = (value == 0 ? false : true);
                            emit logmessage(QString("conf250m[%1].trigger[%2].rising = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                }
                else if (varList.at(1).compare(QString("trigger_block[%1]").arg(index[1]),Qt::CaseInsensitive) == 0  && index[1] >= 0 && index[1] < NDIGO250M_CHANNEL_COUNT + 1)
                {
                    if (varList.at(2).compare(QString("enabled"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf250m[index[0]].trigger_block[index[1]].enabled = (value == 0 ? false : true);
                            emit logmessage(QString("conf250m[%1].trigger_block[%2].enabled = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("force_led"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf250m[index[0]].trigger_block[index[1]].force_led = (value == 0 ? false : true);
                            emit logmessage(QString("conf250m[%1].trigger_block[%2].force_led = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("retrigger"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf250m[index[0]].trigger_block[index[1]].retrigger = (value == 0 ? false : true);
                            emit logmessage(QString("conf250m[%1].trigger_block[%2].retrigger = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("multi_shot_count"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf250m[index[0]].trigger_block[index[1]].multi_shot_count = value;
                            emit logmessage(QString("conf250m[%1].trigger_block[%2].multi_shot_count = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("precursor"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf250m[index[0]].trigger_block[index[1]].precursor = value;
                            emit logmessage(QString("conf250m[%1].trigger_block[%2].precursor = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("length"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf250m[index[0]].trigger_block[index[1]].length = value;
                            emit logmessage(QString("conf250m[%1].trigger_block[%2].length = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("sources"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf250m[index[0]].trigger_block[index[1]].sources = value;
                            emit logmessage(QString("conf250m[%1].trigger_block[%2].sources = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("gates"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf250m[index[0]].trigger_block[index[1]].gates = value;
                            emit logmessage(QString("conf250m[%1].trigger_block[%2].gates = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("minimum_free_packets"),Qt::CaseInsensitive) ==  0 ) {
                        double value = rhs.toDouble(&ok);
                        if(ok)
                        {
                            conf250m[index[0]].trigger_block[index[1]].minimum_free_packets = value;
                            emit logmessage(QString("conf250m[%1].trigger_block[%2].minimum_free_packets = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                }
                else if (varList.at(1).compare(QString("gating_block[%1]").arg(index[1]),Qt::CaseInsensitive) == 0 && index[1] >= 0 && index[1] < NDIGO250M_GATE_COUNT)
                {
                    if (varList.at(2).compare(QString("negate"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf250m[index[0]].gating_block[index[1]].negate = (value == 0 ? false : true);
                            emit logmessage(QString("conf250m[%1].gating_block[%2].negate = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("retrigger"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf250m[index[0]].gating_block[index[1]].retrigger = (value == 0 ? false : true);
                            emit logmessage(QString("conf250m[%1].gating_block[%2].retrigger = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("extend"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf250m[index[0]].gating_block[index[1]].extend = (value == 0 ? false : true);
                            emit logmessage(QString("conf250m[%1].gating_block[%2].extend = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("start"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf250m[index[0]].gating_block[index[1]].start = value;
                            emit logmessage(QString("conf250m[%1].gating_block[%2].start = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("stop"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf250m[index[0]].gating_block[index[1]].stop = value;
                            emit logmessage(QString("conf250m[%1].gating_block[%2].stop = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                    if (varList.at(2).compare(QString("sources"),Qt::CaseInsensitive) ==  0 ) {
                        int value = rhs.toInt(&ok,0);
                        if(ok)
                        {
                            conf250m[index[0]].gating_block[index[1]].sources = value;
                            emit logmessage(QString("conf250m[%1].gating_block[%2].sources = %3").arg(index[0]).arg(index[1]).arg(value));
                            continue;
                        }
                    }
                }
            }
        }
        else emit errormessage(QLatin1String("Couldn't interpret %1").arg(line));
    }

    inputFile.close();
 }

long long acqcontrol::time_CF(short *data_buf, int size)
{
    int max = 0, imax = 0;
    double threshold = 0;
    qint64 found_time = 0;


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
        if (std::abs(data_buf[j - 1]) < threshold&&std::abs(data_buf[j]) >= threshold)
        {
            found_time = (qint64)(800.0*((double)j - 1. + (-fabs((double)data_buf[j - 1]) + (double)threshold) / (fabs((double)data_buf[j]) - fabs((double)data_buf[j - 1]))));
            break;
        }
    }
    return (qint64)found_time;
}
