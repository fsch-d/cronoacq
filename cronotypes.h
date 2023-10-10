#ifndef CRONOTYPES_H
#define CRONOTYPES_H

#include <QString>


typedef struct {
    int sourcecard;
    int sourcechan;
    int range_start;
    int range_stop;
    bool autosize;
    unsigned int filesize;
}  main_pars;

typedef struct {
    int index;
    int precursor;
    int length;
    bool retrigger;
} card_pars;

typedef struct {
    bool edge;
    bool rising;
    int thresh;
} chan_pars;

typedef struct {
    card_pars cardPars;
    chan_pars chan[5];
} ccpars;


typedef struct {
    main_pars main;
    ccpars card[3];
} init_pars;

typedef struct {
    int iRate;
    bool bIsRunning;
    bool bIsConnected;
    bool bFileOpen;
    QString sFileName;
} status_pars;


typedef struct {
    int channel;
    quint64 time;
} bufferEvnt;


typedef struct {
    int trigger_index;
    int flags;
    quint64 timestamp;
    int packet_count;
    char* data; //ndigo data packets
} evnt_group;



#endif // CRONOTYPES_H
