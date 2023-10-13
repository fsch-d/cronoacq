#ifndef CRONOTYPES_H
#define CRONOTYPES_H

#include <QString>
#include <QMetaType>
#include <QDataStream>

#define NR_CARDS 3


typedef struct {
    int sourcecard;
    int sourcechan;
    int range_start;
    int range_stop;
    bool advconf;
    QString advfilename;
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


class init_pars
{
public:
    init_pars() = default;
    ~init_pars() = default;
    init_pars(const init_pars &) = default;
    init_pars &operator=(const init_pars &) = default;
    main_pars main;
    ccpars card[NR_CARDS];

    friend QDataStream& operator<<(QDataStream& out, const init_pars& v)
    {
        out << v.main.sourcecard << v.main.sourcechan << v.main.range_start << v.main.range_stop
            << v.main.advconf << v.main.advfilename;
        for(int i=0; i<NR_CARDS; i++)
        {
            out << v.card[i].cardPars.index << v.card[i].cardPars.precursor
                << v.card[i].cardPars.length << v.card[i].cardPars.retrigger;
            for(int j=0; j<5; j++)
            {
                out  << v.card[i].chan[j].edge << v.card[i].chan[j].rising << v.card[i].chan[j].thresh;
            }
        }
        return out;
    }

    friend QDataStream& operator>>(QDataStream& in, init_pars& v)
    {
        in >> v.main.sourcecard >> v.main.sourcechan >> v.main.range_start >> v.main.range_stop
            >> v.main.advconf >> v.main.advfilename;
        for(int i=0; i<NR_CARDS; i++)
        {
            in  >> v.card[i].cardPars.index >>  v.card[i].cardPars.precursor
                >> v.card[i].cardPars.length >> v.card[i].cardPars.retrigger;
            for(int j=0; j<5; j++)
            {
                in  >> v.card[i].chan[j].edge >> v.card[i].chan[j].rising >> v.card[i].chan[j].thresh;
            }
        }
        return in;
    }


};
Q_DECLARE_METATYPE(init_pars)



struct status_pars {
    int active_card;
    int active_chan;
    //int iRate;
    //bool bIsRunning;
    //bool bIsConnected;
    bool bFileOpen;
    QString sFileName;
    status_pars()
    {
        active_card=-1;
        active_chan=-1;
        bFileOpen=false;
        sFileName=QString("");
    }
};


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
