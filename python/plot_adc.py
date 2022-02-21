#!/usr/bin/python3

import sys
import struct
import argparse

import usb.core

ID_VENDOR, ID_PRODUCT = 0x1A87, 0x5513

EP_READ  = 1

ADC_REQUEST_SETUP           = 1
ADC_TOTAL_CHANNELS          = 10
ADC_MODE_BITS               = 0x0F
ADC_MODE_FREQUENCY          = 0xF0

_invdict = lambda d: dict([(v, k) for (k, v) in d.items()])

ADC_INDEX = {  # <mnemonic> : (<index>, <nbytes>)
    "cmd":          ( 1, 1),
    "channels":     ( 2, 2),
    "bits":         ( 4, 1),
    "frequency":    ( 5, 1),
    "offset":       ( 6, 2),
    "gain":         ( 8, 1),
    "samples":      ( 9, 1),
    "trigger":      (10, 1),
    "trig_channel": (11, 1),
    "trig_level":   (12, 2),
    "trig_offset":  (14, 4),
    "trig_t_min":   (18, 4),
    "trig_t_max":   (22, 4),
    "use_channels": (26, 2),
}

ADC_CMD = {
    0: "stop",
    1: "once",
    2: "continuous"
}
ADC_CMD_INV = _invdict(ADC_CMD)

ADC_FREQUENCY = {
    0: 0,
    1: 857143,
    2: 500000,
    3: 200000,
    4: 100000,
    5: 50000,
    6: 20000,
    7: 10000,
    8: 5000,
    9: 2000,
    10: 1000
}
ADC_FREQUENCY_INV = _invdict(ADC_FREQUENCY)

ADC_TRIGGER = {
    0: "none",
    1: "rising",
    2: "falling",
    3: "threshold",
    4: "strobelo",
    5: "strobehi",
}
ADC_TRIGGER_INV = _invdict(ADC_TRIGGER)

EP_READ |= 0x80

DEV_DESCR = "0x{:04x}:0x{:04x}".format(ID_VENDOR, ID_PRODUCT)

parser = argparse.ArgumentParser()
parser.add_argument('-m', '--mode', type=str, dest='command',
    choices=sorted(ADC_CMD.values()), default="once",
    help="Acquisition mode (default %(default)s)")
parser.add_argument('-c', '--channels', type=int, dest='channels',
    nargs='*', choices=range(ADC_TOTAL_CHANNELS), default=None,
    help="List of channel numbers to be captured")
parser.add_argument('-b', '--bits', type=int, dest='bits',
    choices=[2, 4, 8, 12], default=None,
    help="Sample resolution in bits-per-sample")
parser.add_argument('-f', '--frequency', type=int, dest='frequency',
    choices=sorted(ADC_FREQUENCY.values()), default=None,
    help="Frequency of each of two ADC, actual samplerate is "
    "2 * Frequency / NumberOfChannels")
parser.add_argument('-o', '--offset', type=int, dest='offset',
    default=None,
    help="Zero-level for samples")
parser.add_argument('-g', '--gain', type=int, dest='gain',
    default=None,
    help="Digital gain, 2's power")
parser.add_argument('-s', '--samples', type=int, dest='samples',
    default=None,
    help="Number of samples to be captured after trigger (1024 * 2^<samples>)")
parser.add_argument('-t', '--trigger', type=str, dest='trigger',
    choices=sorted(ADC_TRIGGER.values()), default=None,
    help="Type of trigger event to be monitored")
parser.add_argument('--trig-channel', type=int, dest='trig_channel',
    choices=range(ADC_TOTAL_CHANNELS), default=None,
    help="Number of channel for trigger event")
parser.add_argument('--trig-level', type=int, dest='trig_level',
    default=None,
    help="Trigger level")
parser.add_argument('--trig-offset', type=int, dest='trig_offset',
    default=None,
    help="Time before (<0) or after (>0) trigger event to start capture, "
    "in samples per channel")
parser.add_argument('--trig-t-min', type=int, dest='trig_t_min',
    default=None,
    help="Minimum strobe length in samples for trigger")
parser.add_argument('--trig-t-max', type=int, dest='trig_t_max',
    default=None,
    help="Maximum strobe length in samples for trigger")

parser.add_argument('--timeout', type=float, dest='timeout', default=1.5,
    help="Timeout for acquisition started in seconds (default %(default)s)")
parser.add_argument('--v-ref', type=float, dest='v_ref', default=3.3,
    help="Reference voltage (default %(default)s)")
parser.add_argument('--time-scale', type=float, dest='timescale', default=0.001,
    help="Unit of time in seconds (default %(default)s)")
parser.add_argument('--v-scale', type=float, dest='vscale', default=1.0,
    help="Unit of voltage in volts (default %(default)s)")
parser.add_argument('--max-samples', type=int, dest='max_samples', default=None,
    help="Maximum number of samples to be read (default - until timeout)")

parser.add_argument('--output', type=str, dest='output', default=None,
    help="Output file for tabular data (default - stdout if --plot not given, "
    "or no output otherwise")
parser.add_argument('--plot', action='store_true', dest='plot',
    help="Plot the output with (requires matplotlib.pyplot)")

args = parser.parse_args()

def bits_to_indicies(b):
    ret = []
    for i in range(ADC_TOTAL_CHANNELS):
        if (b & (1 << i)):
            ret.append(i)
    return ret


def indicies_to_bits(indicies):
    ret = 0
    for i in indicies:
        ret |= (1 << i)
    return ret


def configure(dev, var, value):
    index, nbytes = ADC_INDEX[var]
    for i in range(nbytes):
        bval = (value >> (i*8)) & 0xff
        dev.ctrl_transfer(0x40, ADC_REQUEST_SETUP, bval, index + i)


def unpack_data(data, bits):
    ret = []
    scale = args.v_ref / float(0xfff)
    if bits == 2:
        for b in data:
            ret.append((b & 0xc0) <<  4)
            ret.append((b & 0x30) <<  6)
            ret.append((b & 0x0c) <<  8)
            ret.append((b & 0x03) << 10)
    elif bits == 4:
        for b in data:
            ret.append((b & 0xf0) << 4)
            ret.append((b & 0x0f) << 8)
    elif bits == 8:
        for b in data:
            ret.append(b << 4)
    elif bits == 12:
        for i in range(0, len(data), 3):
            b1, b2, b3 = data[i], data[i+1], data[i+2]
            ret.append((b1 << 4) | (b2 >> 4))
            ret.append((b3 << 4) | (b2 & 0xf))
    return [float(x) * scale for x in ret]



last_seq = seq_offset = None
def read_adc(dev):
    global last_seq, seq_offset
    try:
        data = dev.read(EP_READ, 64, int(args.timeout*1000.0))
    except usb.core.USBError as ex:
        return [], {}
    seq, chans, mode = struct.unpack("<BHB", data[:4])
    seq_n = seq & 0x7f
    if last_seq is None or (seq & 0x80):
        last_seq = seq_offset = seq_n - 1
    new_seq = last_seq + (seq_n - last_seq + 0x80) % 0x80
    if new_seq != last_seq + 1:
        print("(lost {} chunk(s)) [seq = 0x{:02x}, n = {}, offset = {}, last = {}, new = {}]".format(
            new_seq - 1 - last_seq, seq, seq_n, seq_offset, last_seq, new_seq))
    last_seq = new_seq
    data = data[4:]
    
    bits = (mode & ADC_MODE_BITS)
    freq = (mode & ADC_MODE_FREQUENCY) >> 4
    chans = bits_to_indicies(chans)
    samples = unpack_data(data, bits)

    samples_per_chan = len(samples) // len(chans)
    samples = samples[:samples_per_chan * len(chans)]
    
    sample_period = 1.0 / float(ADC_FREQUENCY[freq])
    if len(chans) > 1 or (len(chans) == 1 and freq == 1):  # two ADCs in use, double frequency
        sample_period *= 0.5
    dt = sample_period * len(chans)
     
    T_per_seq = samples_per_chan * dt
    
    T0 = T_per_seq * (new_seq - seq_offset)
    ts = [
        (T0 + k*dt) / args.timescale
        for k in range(samples_per_chan)
    ]
    vs = {}

    for i, nch in enumerate(chans):
        ch = "CH.{}".format(nch)
        vs[ch] = [
            samples[k*len(chans) + i] / args.vscale
            for k in range(samples_per_chan)
        ]
    return ts, vs


dev = usb.core.find(idVendor=ID_VENDOR, idProduct=ID_PRODUCT)

if dev is None:
    raise Exception("Device {} not found".format(DEV_DESCR))

if dev.is_kernel_driver_active(0):
    dev.detach_kernel_driver(0)

configure(dev, "cmd", ADC_CMD_INV["stop"])

if args.trig_t_max is not None:
    configure(dev, "trig_t_max", args.trig_t_max)
if args.trig_t_min is not None:
    configure(dev, "trig_t_min", args.trig_t_min)
if args.trig_level is not None:
    configure(dev, "trig_level", args.trig_level)
if args.trig_channel is not None:
    configure(dev, "trig_channel", args.trig_channel)
if args.trig_offset is not None:
    configure(dev, "trig_offset", args.trig_offset)
if args.trigger is not None:
    configure(dev, "trigger", ADC_TRIGGER_INV[args.trigger])
if args.samples is not None:
    configure(dev, "samples", args.samples)
if args.gain is not None:
    configure(dev, "gain", args.gain)
if args.offset is not None:
    configure(dev, "offset", args.offset)
if args.frequency is not None:
    configure(dev, "frequency", ADC_FREQUENCY_INV[args.frequency])
if args.bits is not None:
    configure(dev, "bits", args.bits)
if args.channels is not None:
    configure(dev, "channels", indicies_to_bits(args.channels))


print("clearing buffer ...")
while True:
    xs, vs = read_adc(dev)
    if len(xs) == 0 and len(vs) == 0:
        break

configure(dev, "cmd", ADC_CMD_INV[args.command])

print("waiting for trigger ...")
while True:
    xs, vs = read_adc(dev)
    if len(xs) > 0 and len(vs) > 0:
        break


while args.max_samples is None or len(xs) < args.max_samples:
    print("{} sample(s) read...\r".format(len(xs)), end='')
    new_xs, new_vs = read_adc(dev)
    if len(new_xs) == 0 and len(new_vs) == 0:
        break
    xs.extend(new_xs)
    for ch in new_vs.keys():
        vs[ch].extend(new_vs[ch])

if args.plot:
    from matplotlib import pyplot as plt
    fig, ax = plt.subplots()
    for ch, ys in sorted(vs.items()):
        plt.plot(xs, ys, label=ch)
    legend = ax.legend(loc='upper right', shadow=True, fontsize='x-large')
    legend.get_frame().set_facecolor('#00FFCC')
    plt.xlabel('T [{:.03f} s]'.format(args.timescale))
    plt.ylabel('V [{:.03f} V]'.format(args.vscale))
    plt.grid(True)
    plt.show()

if not args.plot or args.output is not None:
    if args.output is None:
        out = sys.stdout
    else:
        out = open(args.output, 'w')
    
    chans = sorted(vs.keys())
    out.write("\t".join(["T [{:.03f} s]".format(args.timescale)] + 
        ["{} [{:.03f} V]".format(k, args.vscale) for k in chans]))
    out.write('\n')
    for i in range(len(xs)):
        cols = [xs[i]] + [vs[k][i] for k in chans]
        out.write("\t".join([str(v) for v in cols]))
        out.write('\n')
    
    if out != sys.stdout:
        out.close()
