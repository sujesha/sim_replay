#Script for different cache sizes of sector and content caches

#1. Comparison as proportion of content-cache size changes in 1GB total cache
RAM_size=1024
#Pair-wise STANDARD, PROVIDED & CONFIDED for the 7 non-identical VMs
./script.automate-vmpair-spfreplay-varycache-booting.sh v2p_map_boot_nonidentical.txt $RAM_size

#Group-wise STANDARD, PROVIDED & CONFIDED for the 7 non-identical VMs
./script.automate-vmgroup-spfreplay-varycache-booting.sh v2p_map_boot_diffdistros.txt $RAM_size
./script.automate-vmgroup-spfreplay-varycache-booting.sh v2p_map_boot_diffversions.txt $RAM_size
./script.automate-vmgroup-spfreplay-varycache-booting.sh v2p_map_boot_nonidentical.txt $RAM_size

#Group-wise STANDARD, PROVIDED & CONFIDED for the 20 non-identical VMs
#./script.automate-vmgroup-spfreplay-varycache-booting.sh 5-identical-lucid-vmlist.txt $RAM_size
#./script.automate-vmgroup-spfreplay-varycache-booting.sh 10-identical-lucid-vmlist.txt $RAM_size
#./script.automate-vmgroup-spfreplay-varycache-booting.sh 15-identical-lucid-vmlist.txt $RAM_size
#./script.automate-vmgroup-spfreplay-varycache-booting.sh 20-identical-lucid-vmlist.txt $RAM_size

for CCACHE_size in 100 200 300 500 700 900 
do
	#Pair-wise IODEDUP for the 7 non-identical VMs
	./script.automate-vmpair-ioreplay-varycache-booting.sh v2p_map_boot_nonidentical.txt $RAM_size $CCACHE_size

	#Group-wise IODEDUP for the 7 non-identical VMs
	./script.automate-vmgroup-ioreplay-varycache-booting.sh v2p_map_boot_diffdistros.txt $RAM_size $CCACHE_size
	./script.automate-vmgroup-ioreplay-varycache-booting.sh v2p_map_boot_diffversions.txt $RAM_size $CCACHE_size
	./script.automate-vmgroup-ioreplay-varycache-booting.sh v2p_map_boot_nonidentical.txt $RAM_size $CCACHE_size

	#Group-wise IODEDUP for the 20 non-identical VMs
	#./script.automate-vmgroup-ioreplay-varycache-booting.sh 5-identical-lucid-vmlist.txt $RAM_size $CCACHE_size
	#./script.automate-vmgroup-ioreplay-varycache-booting.sh 10-identical-lucid-vmlist.txt $RAM_size $CCACHE_size
	#./script.automate-vmgroup-ioreplay-varycache-booting.sh 15-identical-lucid-vmlist.txt $RAM_size $CCACHE_size
	#./script.automate-vmgroup-ioreplay-varycache-booting.sh 20-identical-lucid-vmlist.txt $RAM_size $CCACHE_size
done

#2. Comparison as size of sector-cache changes with content-cache fixed at 200M
#CCACHE_size=200
#for RAM_size in 256 512 1536 2048 3072
#do
#    #Pair-wise in the 7 non-identical VMs
#    ./script.automate-vmpair-spfreplay-varycache-booting.sh v2p_map_boot_nonidentical.txt $RAM_size
#    ./script.automate-vmpair-ioreplay-varycache-booting.sh v2p_map_boot_nonidentical.txt $RAM_size $CCACHE_size
#
#    #Group-wise in the 7 non-identical VMs
#    ./script.automate-vmgroup-spfreplay-varycache-booting.sh v2p_map_boot_diffdistros.txt $RAM_size
#    ./script.automate-vmgroup-ioreplay-varycache-booting.sh v2p_map_boot_diffdistros.txt $RAM_size $CCACHE_size
#    ./script.automate-vmgroup-spfreplay-varycache-booting.sh v2p_map_boot_diffversions.txt $RAM_size
#    ./script.automate-vmgroup-ioreplay-varycache-booting.sh v2p_map_boot_diffversions.txt $RAM_size $CCACHE_size
#    ./script.automate-vmgroup-spfreplay-varycache-booting.sh v2p_map_boot_nonidentical.txt $RAM_size
#    ./script.automate-vmgroup-ioreplay-varycache-booting.sh v2p_map_boot_nonidentical.txt $RAM_size $CCACHE_size
#
#    #Group-wise in the 20 non-identical VMs
#    #./script.automate-vmgroup-spfreplay-varycache-booting.sh 5-identical-lucid-vmlist.txt $RAM_size
#    #./script.automate-vmgroup-ioreplay-varycache-booting.sh 5-identical-lucid-vmlist.txt $RAM_size $CCACHE_size
#    #./script.automate-vmgroup-spfreplay-varycache-booting.sh 10-identical-lucid-vmlist.txt $RAM_size
#    #./script.automate-vmgroup-ioreplay-varycache-booting.sh 10-identical-lucid-vmlist.txt $RAM_size $CCACHE_size
#    #./script.automate-vmgroup-spfreplay-varycache-booting.sh 15-identical-lucid-vmlist.txt $RAM_size
#    #./script.automate-vmgroup-ioreplay-varycache-booting.sh 15-identical-lucid-vmlist.txt $RAM_size $CCACHE_size
#    #./script.automate-vmgroup-spfreplay-varycache-booting.sh 20-identical-lucid-vmlist.txt $RAM_size
#    #./script.automate-vmgroup-ioreplay-varycache-booting.sh 20-identical-lucid-vmlist.txt $RAM_size $CCACHE_size
#done 

