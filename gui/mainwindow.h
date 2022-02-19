#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QCheckBox>
#include <QFile>
#include <QTimer>
#include <QElapsedTimer>
#include <QImage>
#include <QMap>

#include <libusb-1.0/libusb.h>

#include "adc_proto.h"

#define TRANSFER_COUNT      8
#define TRANSFER_SIZE       (ADC_SAMPLES_COUNT * ADC_PACKET_SIZE * 1)
#define TRANSFER_TIMEOUT_MS 300

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

    QList<QCheckBox *>      channels_box;

    const QColor channels_color[ADC_TOTAL_CHANNELS] = {
        Qt::green,
        Qt::yellow,
        Qt::blue,
        Qt::cyan,
        Qt::red,
        Qt::white,
        Qt::magenta,
        Qt::darkGreen,
        Qt::darkYellow,
        Qt::darkBlue
    };

    QTimer                  event_timer;
    struct libusb_context * ctx;
    libusb_device_handle  * current_adc;
    bool                    restart_transfers;

    struct MemBuf
    {
        bool dev;
        unsigned char * ptr;
        unsigned int length;
    };

    QList<MemBuf>           bufs;
    struct libusb_transfer* transfers[TRANSFER_COUNT];

    int                     last_seq, seq_t0;
    QElapsedTimer           statistic_timer, redraw_timer;
    qulonglong              bytes_received, packets_received,
                            samples_received, periods_received,
                            packets_lost;

    QList<double>           ts_data;
    QMap<int, QList<double> > vs_data;
    int                     channels_in_use;
    bool                    redraw_needed;

    QImage                  plot_bgd;

    QFile                   dump;

    double samplePeriod(int frequency_code);
    void setCurrentADC(libusb_device * device);
    void resetStatistics();
    void updateStatistics(int bytes, int packets, int samples, int periods, int lost);
    void updateData(int packet_num, int freq_code, const QList<int> &channels, const QList<uint16_t> &samples);
    void redrawSamples(bool force = false);

    void parseADCPacket(const unsigned char * packet);

    int32_t readRegister(int reg_index0, int nbytes = 1, int tries = 3);
    void writeRegister(int reg_index0, int32_t reg_value, int nbytes = 1, int tries = 3);

    void readConfig();

public:
    explicit MainWindow(struct libusb_context * ctx0, QWidget *parent = 0);
    ~MainWindow();

    void onTransfer(struct libusb_transfer * transfer);

public slots:
    void handleUsbEvents(int timeout_ms = 0);
    void clearDevicesList();
    void refreshDevicesList();
    void deviceSelected(QAction *action);
    void updateChannelsSelection();

private slots:
    void on_cbNBits_currentIndexChanged(int index);
    void on_cbFrequency_currentIndexChanged(int index);
    void on_cbSamples_currentIndexChanged(int index);
    void on_hsOffset_valueChanged(int value);
    void on_hsGain_valueChanged(int value);
    void on_cbTrigger_currentIndexChanged(int index);
    void on_cbTrigChannel_currentIndexChanged(int index);
    void on_hsTrigLevel_valueChanged(int value);
    void on_dsbTrigOffset_valueChanged(double arg1);
    void on_dsbTrigTMin_valueChanged(double arg1);
    void on_dsbTrigTMax_valueChanged(double arg1);
    void on_cbTScale_currentIndexChanged(int index);
    void on_hsTOffset_GUI_valueChanged(int value);
    void on_hsVOffset_GUI_valueChanged(int value);
    void on_cbVScale_currentIndexChanged(int index);
    void on_pbOnce_clicked();
    void on_pbContinuous_clicked();

private:
    Ui::MainWindow *ui;
};

#endif // MAINWINDOW_H
