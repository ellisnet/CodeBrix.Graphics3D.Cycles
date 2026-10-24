#!/bin/bash
# Merge wave 8 part 2: film identity of every Linux slot against wave 7 part 2 (merge7 p2-*) and against part 1b (b8-*); plus reference/oracle trees.
M=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge8; M7=$M/../merge7; O=$M/validation/film-compare-p2; T=$M/tools
: > $O/summary-vs-wave7.txt; : > $O/summary-vs-part1b.txt
for d in $M/validation/p8-* $M/validation/p8v-*; do l=$(basename $d); suffix=${l#p8}; suffix=${suffix#v}; 
  # wave 7 label: p8-X -> p2-X, p8v-X -> p2v-X ; part 1b label: b8-X / b8v-X
  w7=$M7/validation/p2${l#p8}; b1=$M/validation/b8${l#p8}
  if [ -d $w7 ]; then python3 $T/film_compare.py $w7 $d $O/w7-$l.json > /tmp/fc.$$ 2>&1 && echo "$l $(python3 -c "import json;r=json.load(open('$O/w7-$l.json'));print(r['compared'],r['identical'],len(r['different']),len(r['onlyOld']),len(r['onlyNew']))")" >> $O/summary-vs-wave7.txt || echo "$l ERROR $(tail -1 /tmp/fc.$$)" >> $O/summary-vs-wave7.txt; else echo "$l no-wave7-counterpart" >> $O/summary-vs-wave7.txt; fi
  if [ -d $b1 ]; then python3 $T/film_compare.py $b1 $d $O/b1-$l.json > /tmp/fc.$$ 2>&1 && echo "$l $(python3 -c "import json;r=json.load(open('$O/b1-$l.json'));print(r['compared'],r['identical'],len(r['different']),len(r['onlyOld']),len(r['onlyNew']))")" >> $O/summary-vs-part1b.txt || echo "$l ERROR $(tail -1 /tmp/fc.$$)" >> $O/summary-vs-part1b.txt; fi
done
for t in reference-p2 oracles-p2 bundles-p2; do for pair in "$M7/$t" "$M/${t/p2/1b}"; do [ -d $pair ] && python3 $T/film_compare.py $pair $M/$t $O/tree-$t-vs-$(basename $(dirname $pair))-$(basename $pair).json > /dev/null 2>&1; done; done
echo done > $O/DONE
