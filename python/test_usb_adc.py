#!/usr/bin/python

import sys
import time
import struct

import usb.core

ID_VENDOR, ID_PRODUCT = 0x1A87, 0x5513

EP_READ  = 1

ADC_REQUEST_SETUP           = 1

ADC_INDEX_CMD               = 1
ADC_INDEX_CHANNELS          = 2
ADC_INDEX_BITS              = 4
ADC_INDEX_FREQUENCY         = 5
ADC_INDEX_SAMPLES           = 9
ADC_INDEX_TRIGGER           = 10

EP_READ |= 0x80

DEV_DESCR = "0x{:04x}:0x{:04x}".format(ID_VENDOR, ID_PRODUCT)


last_seq = None
def read_adc(dev, timeout=1.5):
    global last_seq
    data = dev.read(EP_READ, 64, int(timeout*1000.0))
    bytes_recv = len(data)
    
    seq, chans, mode = struct.unpack("<BHB", data[:4])
    seq &= 0x7f
    if last_seq is None:
        last_seq = seq - 1
    new_seq = last_seq + (seq - last_seq + 0x80) % 0x80
    lost = 0
    if new_seq != last_seq + 1:
        lost = new_seq - 1 - last_seq
    last_seq = new_seq
    data = data[4:]
    
    nchans = sum([(chans >> i) & 1 for i in range(10)])
    samples = len(data) * 8 // bits
    intervals = samples // nchans
    
    return bytes_recv, samples, intervals, lost


print("Finding device {}...".format(DEV_DESCR))
dev = usb.core.find(idVendor=ID_VENDOR, idProduct=ID_PRODUCT)

if dev is None:
    raise Exception("Device {} not found".format(DEV_DESCR))
print("Device {} found".format(DEV_DESCR))

if dev.is_kernel_driver_active(0):
    print("Detaching kernel driver")
    dev.detach_kernel_driver(0)

print("set_configuration()")
dev.set_configuration()

freq, bits, chans = 1, 8, 0b1
if len(sys.argv) > 1:
    freq = int(sys.argv[1])
if len(sys.argv) > 2:
    bits = int(sys.argv[2])
if len(sys.argv) > 3:
    chans = int(sys.argv[3], 2)

dev.ctrl_transfer(0x40, ADC_REQUEST_SETUP, 0, ADC_INDEX_CMD)
dev.ctrl_transfer(0x40, ADC_REQUEST_SETUP, 0, ADC_INDEX_TRIGGER)
dev.ctrl_transfer(0x40, ADC_REQUEST_SETUP, 5, ADC_INDEX_SAMPLES)
dev.ctrl_transfer(0x40, ADC_REQUEST_SETUP, chans & 0xff, ADC_INDEX_CHANNELS)
dev.ctrl_transfer(0x40, ADC_REQUEST_SETUP, chans >> 8, ADC_INDEX_CHANNELS+1)
dev.ctrl_transfer(0x40, ADC_REQUEST_SETUP, bits, ADC_INDEX_BITS)
dev.ctrl_transfer(0x40, ADC_REQUEST_SETUP, freq, ADC_INDEX_FREQUENCY)
dev.ctrl_transfer(0x40, ADC_REQUEST_SETUP, 2, ADC_INDEX_CMD)


total_pkt = total_bytes = total_samples = total_intervals = total_lost = 0
all_bytes = all_samples = all_intervals = 0
t0 = time.time()
while True:
    nbytes, samples, intervals, lost = read_adc(dev)
    total_pkt += 1
    total_bytes += nbytes
    total_samples += samples
    total_intervals += intervals
    total_lost += lost
    all_bytes += nbytes * (lost + 1)
    all_samples += samples * (lost + 1)
    all_intervals += intervals * (lost + 1)
    elapsed = time.time() - t0
    if elapsed > 10.0:
        break


def report(prefix, cnt, dt, unit="", base=1000):
    freq = float(cnt) / dt
    if freq > 1.5 * base * base:
        freq /= base * base
        funit = "M{}".format(unit)
    elif freq > 1.5 * base:
        freq /= base
        funit = "k{}".format(unit)
    else:
        funit = unit
    print("{}: {} in {:.3f} sec  [ {:.3f} {}/s ]".format(
        prefix, cnt, dt, freq, funit))


report("Packets", total_pkt, elapsed, "p", 1000)
report("Bytes", total_bytes, elapsed, "b", 1024)
report("Bits", total_bytes*8, elapsed, "bit", 1024)
report("Samples", total_samples, elapsed, "S", 1000)
report("Periods", total_intervals, elapsed, "p", 1000)
report("Lost", total_lost, elapsed, "pkt", 1000)
report("All samples", all_samples, elapsed, "S", 1000)
report("All periods", all_intervals, elapsed, "p", 1000)
print("TOTAL LOSS: {:.2f}%".format(100.0 * total_lost / (total_pkt + total_lost)))
