#ifndef CRONOTYPES_H
#define CRONOTYPES_H

#include <QString>
#include <QMetaType>
#include <QDataStream>

#define NR_CARDS 4 //total of all cards (5G + 250M)
#define NR_250MCARDS 1 //
#define GO4_BUFFERSIZE 512 //number of time signals per online event


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
    bool m_bTrigOnly;
    bool bFileOpen;
    QString sFileName;
    status_pars()
    {
        active_card=-1;
        active_chan=-1;
        m_bTrigOnly=false;
        bFileOpen=false;
        sFileName=QString("");
    }
};


typedef struct {
    qint64 channel;
    qint64 time;
} bufferEvnt;


typedef struct {
    bool validTrigger;
    int trigger_index;
    int flags;
    qint64 timestamp;
    int packet_count;
} evnt_group;



#endif // CRONOTYPES_H
