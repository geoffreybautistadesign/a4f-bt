#!/usr/bin/env python3
# Envoie du PCM s16 stéréo 48 kHz en paquets A4F (a4f_protocol.h) vers a4f_a2dp_source, au rythme réel.
#   python3 pcm_send.py                 -> mélodie de test (gauche, puis droite, puis les deux)
#   python3 pcm_send.py fichier.wav     -> WAV s16 stéréo 48 kHz (afconvert -f WAVE -d LEI16@48000 -c 2 in out.wav)
import math, socket, struct, sys, time, wave

HOST, PORT = "127.0.0.1", 9051
RATE, CHUNK = 48000, 256
MAGIC_AUDIO, VERSION = 0x41463441, 1


def melody():
    # do-mi-sol-do, 250 ms par note ; 1re fois à gauche, 2e à droite, 3e au centre, en boucle
    notes = [261.63, 329.63, 392.00, 523.25]
    n_note = RATE // 4
    t = 0
    while True:
        for side in ((1, 0), (0, 1), (1, 1)):
            for f in notes:
                frames = bytearray()
                for i in range(n_note):
                    env = min(1.0, i / 480, (n_note - i) / 480)   # fondu 10 ms, pas de clics
                    v = int(9000 * env * math.sin(2 * math.pi * f * t / RATE))
                    frames += struct.pack("<hh", v * side[0], v * side[1])
                    t += 1
                yield bytes(frames)


def wav_file(path):
    w = wave.open(path, "rb")
    if (w.getframerate(), w.getnchannels(), w.getsampwidth()) != (RATE, 2, 2):
        sys.exit("le WAV doit etre s16 stereo 48 kHz : afconvert -f WAVE -d LEI16@48000 -c 2 entree sortie.wav")
    while True:
        data = w.readframes(RATE)
        if not data:
            w.rewind()
            continue
        yield data


def main():
    src = wav_file(sys.argv[1]) if len(sys.argv) > 1 else melody()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    seq, sent, t0 = 0, 0, time.monotonic()
    pending = b""
    print(f"envoi vers {HOST}:{PORT} (Ctrl-C pour arreter)")
    for block in src:
        pending += block
        while len(pending) >= CHUNK * 4:
            payload, pending = pending[:CHUNK * 4], pending[CHUNK * 4:]
            sock.sendto(struct.pack("<IBBHII", MAGIC_AUDIO, VERSION, 0, CHUNK, seq, 0) + payload, (HOST, PORT))
            seq += 1
            sent += CHUNK
            # rythme temps réel, avec 20 ms d'avance au maximum
            delay = t0 + sent / RATE - time.monotonic() - 0.02
            if delay > 0:
                time.sleep(delay)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
