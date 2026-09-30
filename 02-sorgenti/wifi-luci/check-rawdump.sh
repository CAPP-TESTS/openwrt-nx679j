#!/bin/sh
# Confronta sda11 (rawdump, 8:11) a vari offset con il contenuto atteso del v55.
# I first-1MB locali del v55 a quegli offset (forniti come argomenti: off md5).
n=/tmp/rawdump; mknod $n b 8 11 2>/dev/null
for pair in "1048576:a63ac68c82e753a42f0911c3af27e71b" "16777216:11f6fb2740d55f8ef756d02311a79aeb" "33554432:fd525c66bb711bc42eb219fb26dd6f27" "50331648:1792a2e79f06873239b467fa9b3521b6"; do
  off=${pair%%:*}; want=${pair#*:}
  got=$(dd if=$n bs=1 skip=$off count=1048576 2>/dev/null | md5sum | cut -d' ' -f1)
  echo "offset $off: got=$got want=$want"
done
