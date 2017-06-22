set terminal X11 
set title "" 
set xlabel "" 
set ylabel "" 
set zlabel "" 
unset logscale x 
unset logscale y 
unset logscale z 
plot [:] [-1.0:5.0] [:] '/home/sujesha/Documents/APS/provided-eval/datadumping/pdd_replay/intersection-in-overlap-blks.boot-centos-6-4.dat' using 2:1  with dots    , '/home/sujesha/Documents/APS/provided-eval/datadumping/pdd_replay/overlap-blks-for-chunk.boot-centos-6-4.dat' using 2:1  with dots    , '/home/sujesha/Documents/APS/provided-eval/datadumping/pdd_replay/overlap-blks-for-fixed.boot-centos-6-4.dat' using 2:1  with dots     
pause -1
