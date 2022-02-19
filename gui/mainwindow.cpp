#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QPainter>
#include <QCheckBox>

#include <math.h>

static QList<int> bits(uint16_t v)
{
    QList<int> ret;
    for (int i = 0; i < 16; i++)
        if (v & (1 << i))
            ret.push_back(i);
    return ret;
}

static double round_to(double value, int ndigits)
{
    double base = 1.0;
    for (; ndigits > 0; ndigits--)
        base *= 0.1;
    for (; ndigits < 0; ndigits++)
        base *= 10.0;
    return round(value * base) / base;
}

static void get_scale(double vmin, double vmax, int grid_count, double *scalemin, double *scalemax, double *grid0, double *grid_step)
{
    *scalemin = vmin;
    *scalemax = vmax;

    double d = *scalemax - *scalemin;
    if (d == 0)
    {
        *scalemin -= 0.5;
        *scalemax += 0.5;
    }

    double step = (*scalemax - *scalemin) / (double)qMax(1, grid_count);
    int ndigits = floor(log10(fabs(step)));
    *grid_step = round_to(step, ndigits);
    *grid0 = round_to(*scalemin, ndigits);
}

#ifdef __WIN32__
static void __stdcall transfer_callback(struct libusb_transfer * transfer)
#else
static void transfer_callback(struct libusb_transfer * transfer)
#endif
{
    MainWindow * window = (MainWindow*)transfer->user_data;
    window->onTransfer(transfer);
}

double MainWindow::samplePeriod(int frequency_code)
{
    int freq = 0;
    switch (frequency_code)
    {
    default:
    case ADC_FREQUENCY_OFF:
        return 0.0;
    case ADC_FREQUENCY_MAX:
        freq = 857143;
        break;
    case ADC_FREQUENCY_500KHZ:
        freq = 500000;
        break;
    case ADC_FREQUENCY_200KHZ:
        freq = 200000;
        break;
    case ADC_FREQUENCY_100KHZ:
        freq = 100000;
        break;
    case ADC_FREQUENCY_50KHZ:
        freq = 50000;
        break;
    case ADC_FREQUENCY_20KHZ:
        freq = 20000;
        break;
    case ADC_FREQUENCY_10KHZ:
        freq = 10000;
        break;
    case ADC_FREQUENCY_5KHZ:
        freq = 5000;
        break;
    case ADC_FREQUENCY_2KHZ:
        freq = 2000;
        break;
    case ADC_FREQUENCY_1KHZ:
        freq = 1000;
        break;
    }
    double ret = 1.0 / (double)freq;
    if (channels_in_use > 1) // two ADCs in Dual mode
        ret *= 0.5;
    else if (channels_in_use == 1 && frequency_code == ADC_FREQUENCY_MAX) // two ADCs in Fast interleave mode
        ret *= 0.5;
    ret *= (double)channels_in_use;
    return ret;
}

void MainWindow::setCurrentADC(libusb_device *device)
{
    int res;

    if (!current_adc && !device)
        return;
    if (current_adc && libusb_get_device(current_adc) == device)
        return;
    if (current_adc)
    {
        restart_transfers = false;
        for (int i = 0; i < TRANSFER_COUNT; i++)
            libusb_cancel_transfer(transfers[i]);
        handleUsbEvents(TRANSFER_TIMEOUT_MS);
        for (int i = 0; i < TRANSFER_COUNT; i++)
            libusb_free_transfer(transfers[i]);

        for (int i = 0; i < bufs.size(); i++)
            if (bufs[i].dev)
                libusb_dev_mem_free(current_adc, bufs[i].ptr, bufs[i].length);
            else
                free(bufs[i].ptr);
        bufs.clear();

        libusb_close(current_adc);
        current_adc = NULL;
    }

    if (!device)
    {
        ui->statusBar->showMessage(tr("disconnected"));
        return;
    }

    if ((res = libusb_open(device, &current_adc)) != 0)
    {
        current_adc = NULL;
        qDebug("Error opening device: code = %d", res);
        return;
    }

    if (libusb_kernel_driver_active(current_adc, 0))
    {
        if ((res = libusb_detach_kernel_driver(current_adc, 0)) != 0)
        {
            qDebug("Error detaching kernel driver: code = %d", res);
            return;
        }
    }

    if ((res = libusb_set_configuration(current_adc, 1)) != 0)
        qDebug("Error setting configuration: code = %d", res);
    if ((res = libusb_claim_interface(current_adc, 0)) != 0)
        qDebug("Error claiming interface: code = %d", res);

    int dev_bufs = 0;
    for (int i = 0; i < TRANSFER_COUNT; i++)
    {
        MemBuf buf;
        buf.ptr = libusb_dev_mem_alloc(current_adc, TRANSFER_SIZE);
        if (!(buf.dev = (buf.ptr != NULL)))
            buf.ptr = (unsigned char*)calloc(1, TRANSFER_SIZE);
        else
            dev_bufs++;
        buf.length = TRANSFER_SIZE;
        bufs.append(buf);
    }

    for (int i = 0; i < TRANSFER_COUNT; i++)
    {
        if (!(transfers[i] = libusb_alloc_transfer(0)))
        {
            qDebug("Can't allocate transfer #%d", i);
            break;
        }
        libusb_fill_bulk_transfer(transfers[i], current_adc, ADC_SAMPLES_EP | 0x80,
                                  bufs[i].ptr, bufs[i].length,
                                  transfer_callback,
                                  (void*)this, TRANSFER_TIMEOUT_MS);
    }

    readConfig();

    restart_transfers = true;
    resetStatistics();
    for (int i = 0; i < TRANSFER_COUNT; i++)
    {
        if ((res = libusb_submit_transfer(transfers[i])) != 0)
            qDebug("Can't submit transfer %d: error code %d", i, res);
    }
}

void MainWindow::updateStatistics(int bytes, int packets, int samples, int periods, int lost)
{
    bytes_received += bytes;
    packets_received += packets;
    samples_received += samples;
    periods_received += periods;
    packets_lost += lost;

    if (statistic_timer.elapsed() > 2000)
    {
        double dt = (double)statistic_timer.elapsed() * 0.001;
        double kbps = (double)bytes_received * 8.0 / 1024.0 / dt;
        double ksmps = (double)periods_received / 1000.0 / dt;
        double loss = 100.0 * (double)packets_lost / (double)(packets_lost + packets_received);

        ui->statusBar->showMessage(
                    tr("%1 kBit/s; %2 kS/s; loss %3%")
                    .arg(kbps, 0, 'f', 1)
                    .arg(ksmps, 0, 'f', 2)
                    .arg(loss, 0, 'f', 1));

        resetStatistics();
    }
}

void MainWindow::redrawSamples(bool force)
{
    if (!force && (!redraw_needed || redraw_timer.elapsed() < 40))
        return;
    redraw_timer.start();

    int W = ui->lPlot->width(),
        H = ui->lPlot->height();
    QImage plot = QImage(W, H, QImage::Format_ARGB32_Premultiplied);

    QPainter painter(&plot);
    painter.fillRect(0, 0, W, H, Qt::black);

    if (ts_data.size() == 0)
        return;

    QStringList tscale_parts = ui->cbTScale->currentText().split(' ');
    double tscale = tscale_parts[0].toDouble();
    if (tscale_parts[1] == "us")
        tscale *= 1e-6;
    else if (tscale_parts[1] == "ms")
        tscale *= 1e-3;
    double first_t = ts_data.first(), last_t = ts_data.last();
    double tscale_offset = (double)ui->hsTOffset_GUI->value() / 1000.0 * (last_t - first_t);
    double Tmin = first_t, Tmax = first_t + tscale, Tgrid0, Tstep;
    get_scale(Tmin + tscale_offset, Tmax + tscale_offset, 10, &Tmin, &Tmax, &Tgrid0, &Tstep);

    QStringList vscale_parts = ui->cbVScale->currentText().split(' ');
    double vscale = vscale_parts[0].toDouble();
    if (vscale_parts[1] == "uV")
        vscale *= 1e-6;
    else if (vscale_parts[1] == "mV")
        vscale *= 1e-3;
    double vscale_offset = (double)ui->hsVOffset_GUI->value() / 1000.0 * ui->dsbVRef->value();
    double Vmin = 0.0, Vmax = vscale, Vgrid0, Vstep;

    get_scale(Vmin + vscale_offset, Vmax + vscale_offset, 10, &Vmin, &Vmax, &Vgrid0, &Vstep);

    const int margin = 15;

    painter.setPen(QPen(Qt::white, 1));
    painter.drawText(QRect(0, 0, W, H),
                     Qt::AlignLeft | Qt::AlignTop,
                     tr("TIME: %1 [ms] + %2  [ms/Cell];  "
                        "V: %3 [mV] + %4  [mV/Cell]")
                     .arg(Tgrid0 * 1000.0, 7, 'f', 3)
                     .arg(Tstep  * 1000.0, 7, 'f', 3)
                     .arg(Vgrid0 * 1000.0, 7, 'f', 3)
                     .arg(Vstep  * 1000.0, 7, 'f', 3));

    int minx = margin, maxx = W - margin;
    int miny = margin, maxy = H - margin;

    for (double t = Tgrid0; t <= Tmax; t += Tstep)
    {
        int x = minx + (maxx - minx) * (t - Tmin) / (Tmax - Tmin);
        if (t == Tgrid0)
            painter.setPen(QPen(Qt::lightGray, 3));
        else
            painter.setPen(QPen(Qt::darkGray, 1));
        painter.drawLine(x, margin, x, H - margin);
    }
    for (double v = Vgrid0; v <= Vmax; v += Vstep)
    {
        int y = maxy - (maxy - miny) * (v - Vmin) / (Vmax - Vmin);
        if (v == Vgrid0)
            painter.setPen(QPen(Qt::lightGray, 3));
        else
            painter.setPen(QPen(Qt::darkGray, 1));
        painter.drawLine(margin, y, W - margin, y);
    }

    {
        double level = (double)ui->hsTrigLevel->value() / (double)ADC_MAX_LEVEL * ui->dsbVRef->value();
        if (ui->cbTrigger->currentIndex() > 0)
            painter.setPen(QPen(Qt::lightGray, 3, Qt::DotLine));
        else
            painter.setPen(QPen(Qt::darkGray, 1, Qt::DotLine));
        int y = maxy - (maxy - miny) * (level - Vmin) / (Vmax - Vmin);
        painter.drawLine(margin, y, W - margin, y);
    }

    QList<QPolygon> polys;

    QList<int> channel_nums = vs_data.keys();
    for (int j = 0; j < channel_nums.size(); j++)
        polys.push_back(QPolygon());

    for (int i = 0; i < ts_data.size(); i++)
    {
        int x = minx + (maxx - minx) * (ts_data[i] - Tmin) / (Tmax - Tmin);
        for (int j = 0; j < channel_nums.size(); j++)
        {
            int ch = channel_nums[j];
            if (vs_data[ch].size() <= i)
                continue;
            int y = maxy - (maxy - miny) * (vs_data[ch][i] - Vmin) / (Vmax - Vmin);
            polys[j].push_back(QPoint(x, y));
        }
    }

    for (int j = 0; j < channel_nums.size(); j++)
    {
        int ch = channel_nums[j];
        painter.setPen(QPen(channels_color[ch], 1));
        painter.drawPolyline(polys[j]);
    }

    painter.end();

    ui->lPlot->setPixmap(QPixmap::fromImage(plot));
    ui->lPlot->repaint();

    redraw_needed = false;
}

void MainWindow::resetStatistics()
{
    statistic_timer.start();
    bytes_received = packets_received = samples_received = periods_received = 0;
    packets_lost = 0;
}

void MainWindow::updateData(int packet_num, int freq_code, const QList<int> &channels, const QList<uint16_t> &samples)
{
    double dt = samplePeriod(freq_code);
    double packet_period = dt / channels.size() * samples.size();
    double t0 = (double)packet_num * packet_period;

    if (ts_data.size() > 0 && t0 < ts_data.last())
    {
        ts_data.clear();
        vs_data.clear();
        redraw_needed = true;
    }

    if (!ui->pbDoDump->isChecked() && dump.isOpen())
        dump.close();
    if (ui->pbDoDump->isChecked() && !(dump.fileName() == ui->leDumpPath->text() && dump.isOpen()))
    {
        if (dump.isOpen())
            dump.close();
        dump.setFileName(ui->leDumpPath->text());
        dump.open(QIODevice::WriteOnly);

        dump.write("T[ms]");
        for (int ch = 0; ch < channels.size(); ch++)
        {
            int ch_num = channels[ch];
            if (channels_box[ch_num]->isChecked())
                dump.write(QString("\tCH.%1").arg(ch_num + 1).toLatin1());
        }
        dump.write("\n");
    }

    for (int i = 0; i < samples.size() / channels.size(); i++)
    {
        double t = t0 + i * dt;
        ts_data.append(t);
        if (dump.isOpen())
            dump.write(QString("%1").arg(t * 1e3, 0, 'f', 3).toLatin1());
        for (int ch = 0; ch < channels.size(); ch++)
        {
            int ch_num = channels[ch];
            if (!channels_box[ch_num]->isChecked())
                continue;
            double v = (double)samples[i * channels.size() + ch] / (double)ADC_MAX_LEVEL * ui->dsbVRef->value();
            if (!vs_data.contains(ch_num))
                vs_data[ch_num] = QList<double>();
            vs_data[ch_num].append(v);
            if (dump.isOpen())
                dump.write(QString("\t%1").arg(v * 1e3, 0, 'f', 3).toLatin1());
        }
        if (dump.isOpen())
            dump.write("\n");
    }
    redraw_needed = (samples.size() > 0);
}

void MainWindow::parseADCPacket(const unsigned char *packet)
{
    ADCPacketHeader * header = (ADCPacketHeader*)packet;
    uint8_t * data = (uint8_t*)packet + sizeof(ADCPacketHeader);
    int length = ADC_PACKET_SIZE - sizeof(ADCPacketHeader);

    int lost = 0;

    int seq_n = (header->sequence & 0x7f);
    if (last_seq < 0 || (header->sequence & 0x80))
    {
        if (last_seq >= 0)
            redrawSamples();
        seq_t0 = seq_n;
        last_seq = seq_n;
    }
    else
    {
        uint8_t next_seq = (uint8_t)(last_seq + 1);
        lost = (int)((seq_n - next_seq + 0x80) & 0x7f);
        last_seq += lost + 1;
    }

    QList<int> channels = bits(header->channels);
    int nbits = (header->mode & ADC_MODE_BITS);
    int freq_code = (header->mode & ADC_MODE_FREQUENCY) >> 4;

    if (channels.size() == 0)
        return;

    QList<uint16_t> samples;
    int i;
    switch (nbits)
    {
    default:
        return;
    case ADC_BITS_DIGITAL:
        for (i = 0; i < length; i++)
        {
            samples.push_back((uint16_t)(data[i] & 0xc0) <<  4);
            samples.push_back((uint16_t)(data[i] & 0x30) <<  6);
            samples.push_back((uint16_t)(data[i] & 0x0c) <<  8);
            samples.push_back((uint16_t)(data[i] & 0x03) << 10);
        }
        break;
    case ADC_BITS_LO:
        for (i = 0; i < length; i++)
        {
            samples.push_back((uint16_t)(data[i] & 0xf0) << 4);
            samples.push_back((uint16_t)(data[i] & 0x0f) << 8);
        }
        break;
    case ADC_BITS_MID:
        for (i = 0; i < length; i++)
            samples.push_back((uint16_t)data[i] << 4);
        break;
    case ADC_BITS_HI:
        for (i = 0; i < length; i += 3)
        {
            samples.push_back(((uint16_t)data[i+0] << 4) | (((uint16_t)data[i+1] >> 4) & 0x0f));
            samples.push_back(((uint16_t)data[i+2] << 4) | (((uint16_t)data[i+1] >> 0) & 0x0f));
        }
        break;
    }

    updateData(last_seq - seq_t0, freq_code, channels, samples);
    updateStatistics(ADC_PACKET_SIZE, 1, samples.size(), samples.size() / channels.size(), lost);
}

int32_t MainWindow::readRegister(int reg_index0, int nbytes, int tries)
{
    if (!current_adc)
        return 0;
    uint32_t reg_value = 0;
    for (int i = 0; i < nbytes; i++)
    {
        uint8_t value = 0;
        int index = reg_index0 + i;
        for (int ntry = 0; ntry < tries; ntry++)
        {
            int res = libusb_control_transfer(current_adc, 0x80|0x40, ADC_REQUEST_SETUP, 0, index, (unsigned char*)&value, 1, TRANSFER_TIMEOUT_MS);
            if (res < 0)
                qDebug("[%d/%d] [index = %d+%d] libusb_control_transfer() => %d", ntry+1, tries, reg_index0, i, res);
            else
            {
                reg_value |= ((uint32_t)value << (i*8));
                break;
            }
        }
    }
    return (int32_t)reg_value;
}

void MainWindow::writeRegister(int reg_index0, int32_t reg_value, int nbytes, int tries)
{
    if (!current_adc)
        return;
    for (int i = 0; i < nbytes; i++)
    {
        int index = reg_index0 + i;
        uint8_t value = (reg_value >> (i*8)) & 0xff;
        for (int ntry = 0; ntry < tries; ntry++)
        {
            int res = libusb_control_transfer(current_adc, 0x40, ADC_REQUEST_SETUP, value, index, NULL, 0, TRANSFER_TIMEOUT_MS);
            if (res < 0)
                qDebug("[%d/%d] [index = %d+%d, value = 0x%02x] libusb_control_transfer() => %d", ntry+1, tries, reg_index0, i, value, res);
            else
                break;
        }
    }
}

void MainWindow::readConfig()
{
    ui->pbContinuous->setChecked(readRegister(ADC_INDEX_CMD) == ADC_CMD_CONTINUOUS);

    int32_t channels = readRegister(ADC_INDEX_CHANNELS, 2);
    int32_t use_channels = readRegister(ADC_INDEX_USE_CHANNELS, 2);
    channels_in_use = 0;
    for (int i = 0; i < channels_box.size(); i++)
    {
        channels_box[i]->setChecked(channels & (1 << i));
        if (use_channels & (1 << i))
            channels_in_use++;
    }

    switch (readRegister(ADC_INDEX_BITS))
    {
    case ADC_BITS_DIGITAL:
        ui->cbNBits->setCurrentIndex(0);
        break;
    case ADC_BITS_LO:
        ui->cbNBits->setCurrentIndex(1);
        break;
    case ADC_BITS_MID:
        ui->cbNBits->setCurrentIndex(2);
        break;
    case ADC_BITS_HI:
        ui->cbNBits->setCurrentIndex(3);
        break;
    }

    ui->cbFrequency->setCurrentIndex(readRegister(ADC_INDEX_FREQUENCY));
    ui->cbSamples->setCurrentIndex(readRegister(ADC_INDEX_SAMPLES));
    ui->hsOffset->setValue(readRegister(ADC_INDEX_OFFSET, 2));
    ui->hsGain->setValue(readRegister(ADC_INDEX_GAIN));
    ui->cbTrigger->setCurrentIndex(readRegister(ADC_INDEX_TRIGGER));
    ui->cbTrigChannel->setCurrentIndex(readRegister(ADC_INDEX_TRIG_CHANNEL));
    ui->hsTrigLevel->setValue(readRegister(ADC_INDEX_TRIG_LEVEL, 2));

    double dt = samplePeriod(ui->cbFrequency->currentIndex()) * 1000.0;
    ui->dsbTrigOffset->setValue(dt * (double)readRegister(ADC_INDEX_TRIG_OFFSET, 4));
    ui->dsbTrigTMin->setValue(dt * (double)readRegister(ADC_INDEX_TRIG_T_MIN, 4));
    ui->dsbTrigTMax->setValue(dt * (double)readRegister(ADC_INDEX_TRIG_T_MAX, 4));
}

MainWindow::MainWindow(libusb_context *ctx0, QWidget *parent) :
    QMainWindow(parent),
    ctx(ctx0),
    current_adc(NULL),
    restart_transfers(false),
    last_seq(-1),
    channels_in_use(0),
    redraw_needed(true),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    connect(&event_timer, SIGNAL(timeout()), this, SLOT(handleUsbEvents()));
    connect(ui->menuDevice, SIGNAL(triggered(QAction*)), this, SLOT(deviceSelected(QAction*)));

    QGridLayout * grid = new QGridLayout();
    for (int nch = 0; nch < ADC_TOTAL_CHANNELS; nch++)
    {
        QColor color = channels_color[nch];
        QString name = tr("CH.%1").arg(nch+1);
        ui->cbTrigChannel->addItem(name);
        QCheckBox * box = new QCheckBox(name);
        box->setStyleSheet(QString("background-color: rgb(%1, %2, %3);")
                           .arg(color.red())
                           .arg(color.green())
                           .arg(color.blue()));
        grid->addWidget(box, nch / 3, nch % 3);
        connect(box, SIGNAL(clicked(bool)), this, SLOT(updateChannelsSelection()));
        channels_box.push_back(box);
    }
    grid->setSpacing(0);
    ui->gbChannels->setLayout(grid);

    event_timer.setInterval(20);
    event_timer.start();

    redraw_timer.start();

    refreshDevicesList();
}

MainWindow::~MainWindow()
{
    setCurrentADC(NULL);
    libusb_exit(ctx);
    delete ui;
}

void MainWindow::onTransfer(libusb_transfer *transfer)
{
    if (transfer->status != LIBUSB_TRANSFER_COMPLETED && transfer->status != LIBUSB_TRANSFER_TIMED_OUT)
    {
        qDebug("transfer 0x%08llx not completed, status = %d", (qulonglong)transfer, transfer->status);
    }
    else
    {
        for (int i = 0; i + (int)ADC_PACKET_SIZE <= transfer->actual_length; i += ADC_PACKET_SIZE)
            parseADCPacket(transfer->buffer + i);
        if (transfer->status == LIBUSB_TRANSFER_TIMED_OUT)
            redrawSamples();
    }
    if (!restart_transfers)
        return;

    int res = libusb_submit_transfer(transfer);
    if (res != 0)
        qDebug("Can't submit transfer 0x%08llx, code = %d", (qulonglong)transfer, res);
}

void MainWindow::handleUsbEvents(int timeout_ms)
{
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int res = libusb_handle_events_timeout(ctx, &tv);
    if (res != 0)
        qDebug("libusb_handle_events() returns error code %d", res);
}

void MainWindow::clearDevicesList()
{
    foreach (QAction * action, ui->menuDevice->actions())
    {
        libusb_device * dev = (libusb_device*)action->data().toULongLong();
        if (dev)
            libusb_unref_device(dev);
    }
    ui->menuDevice->clear();
    ui->menuDevice->addAction(tr("Refresh list"));
    ui->menuDevice->addSeparator();
}

void MainWindow::refreshDevicesList()
{
    clearDevicesList();

    libusb_device **list = NULL;
    ssize_t count = libusb_get_device_list(ctx, &list);
    int adcs_added = 0;
    for (int i = 0; i < count; i++)
    {
        struct libusb_device_descriptor desc;
        libusb_get_device_descriptor(list[i], &desc);
        if (desc.idVendor == ADC_ID_VENDOR && desc.idProduct == ADC_ID_PRODUCT)
        {
            QString name = tr("bus = %1, port = %2, address = %3")
                    .arg(libusb_get_bus_number(list[i]))
                    .arg(libusb_get_port_number(list[i]))
                    .arg(libusb_get_device_address(list[i]));
            QAction *act = ui->menuDevice->addAction(name);
            act->setData((qulonglong)list[i]);
            adcs_added++;
        }
        else
            libusb_unref_device(list[i]);
    }
    libusb_free_device_list(list, 0);

    if (adcs_added == 1)
        deviceSelected(ui->menuDevice->actions().last());
}

void MainWindow::deviceSelected(QAction *action)
{
    libusb_device * dev = (libusb_device*)action->data().toULongLong();
    if (!dev)
        refreshDevicesList();
    else
        setCurrentADC(dev);
}

void MainWindow::updateChannelsSelection()
{
    uint32_t channels = 0;
    for (int i = 0; i < channels_box.size(); i++)
        if (channels_box[i]->isChecked())
            channels |= (1 << i);
    writeRegister(ADC_INDEX_CHANNELS, channels, 2);
    readConfig();
}

void MainWindow::on_cbNBits_currentIndexChanged(int index)
{
    switch (index)
    {
    case 0:
        writeRegister(ADC_INDEX_BITS, ADC_BITS_DIGITAL);
        break;
    case 1:
        writeRegister(ADC_INDEX_BITS, ADC_BITS_LO);
        break;
    case 2:
        writeRegister(ADC_INDEX_BITS, ADC_BITS_MID);
        break;
    case 3:
        writeRegister(ADC_INDEX_BITS, ADC_BITS_HI);
        break;
    default:
        break;
    }
}

void MainWindow::on_cbFrequency_currentIndexChanged(int index)
{
    writeRegister(ADC_INDEX_FREQUENCY, index);
    readConfig();
}

void MainWindow::on_cbSamples_currentIndexChanged(int index)
{
    writeRegister(ADC_INDEX_SAMPLES, index);
}

void MainWindow::on_hsOffset_valueChanged(int value)
{
    writeRegister(ADC_INDEX_OFFSET, value, 2);
}

void MainWindow::on_hsGain_valueChanged(int value)
{
    writeRegister(ADC_INDEX_GAIN, value);
}

void MainWindow::on_cbTrigger_currentIndexChanged(int index)
{
    writeRegister(ADC_INDEX_TRIGGER, index);
}

void MainWindow::on_cbTrigChannel_currentIndexChanged(int index)
{
    writeRegister(ADC_INDEX_TRIG_CHANNEL, index);
}

void MainWindow::on_hsTrigLevel_valueChanged(int value)
{
    writeRegister(ADC_INDEX_TRIG_LEVEL, value, 2);
    redraw_needed = true;
}

void MainWindow::on_dsbTrigOffset_valueChanged(double arg1)
{
    double dt = samplePeriod(ui->cbFrequency->currentIndex());
    if (dt == 0)
        return;
    int offset = int(arg1 * 0.001 / dt);
    writeRegister(ADC_INDEX_TRIG_OFFSET, offset, 4);
}

void MainWindow::on_dsbTrigTMin_valueChanged(double arg1)
{
    double dt = samplePeriod(ui->cbFrequency->currentIndex());
    if (dt == 0)
        return;
    int t_min = int(arg1 * 0.001 / dt);
    writeRegister(ADC_INDEX_TRIG_T_MIN, t_min, 4);
}

void MainWindow::on_dsbTrigTMax_valueChanged(double arg1)
{
    double dt = samplePeriod(ui->cbFrequency->currentIndex());
    if (dt == 0)
        return;
    int t_max = int(arg1 * 0.001 / dt);
    writeRegister(ADC_INDEX_TRIG_T_MAX, t_max, 4);
}

void MainWindow::on_cbTScale_currentIndexChanged(int)
{
    redrawSamples(true);
}

void MainWindow::on_hsTOffset_GUI_valueChanged(int)
{
    redrawSamples(true);
}

void MainWindow::on_hsVOffset_GUI_valueChanged(int)
{
    redrawSamples(true);
}

void MainWindow::on_cbVScale_currentIndexChanged(int)
{
    redrawSamples(true);
}

void MainWindow::on_pbOnce_clicked()
{
    ui->pbContinuous->setChecked(false);
    writeRegister(ADC_INDEX_CMD, ADC_CMD_ONCE);

}

void MainWindow::on_pbContinuous_clicked()
{
    writeRegister(ADC_INDEX_CMD,
                      ui->pbContinuous->isChecked() ?
                          ADC_CMD_CONTINUOUS :
                          ADC_CMD_STOP);
}
