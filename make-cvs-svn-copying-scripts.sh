#!/bin/sh
#This script is supposed to run on IBM kvm-test machine. Thats why it 
# has not been given chmod x permission here!

PROVIDED_SVN="sujesha@10.129.41.60:Documents/APS/provided-eval/"
PW_SVN="enoughisenough"
PROVIDED_CVS="sujesha@9.126.108.103:provided/"
PW_CVS="intern1@IBM"
PROVIDED_LOCAL="provided\\"

if [ $# -ne 2 ]
then
	echo "Usage: $0 <dir=1|2> <file-with-pathnames>"
	echo "dir=1 => svn to cvs"
	echo "dir=2 => cvs to svn"
	echo "Scripts are supposed to run from 
C:\Documents and Settings\Administrator> since pscp is located there. So,
PROVIDED pathname is $PROVIDED_LOCAL relative to pscp location."
	echo
	echo "PROVIDED pathname in SVN is $PROVIDED_SVN"
	echo "PROVIDED pathname in CVS is $PROVIDED_CVS"
	exit
fi

dir=`expr $1 + 0`
filename=$2

if [ ! -f $filename ]
then
	echo "No file by name $filename"
	exit
fi

if [ $dir -ne 1 -a $dir -ne 2 ]
then
	echo "dir = 1 or 2 only"
	echo "dir=$dir is invalid option"
	exit
fi

if [ $dir -eq 1 ]
then
	SCRIPTNAME="fromsvn-`date|awk '{print $2$3$4}'`.dat"
	PROVIDED_SRC=$PROVIDED_SVN
	PW_SRC=$PW_SVN
	PROVIDED_DST=$PROVIDED_CVS
	PW_DST=$PW_CVS
else
	SCRIPTNAME="fromcvs-`date|awk '{print $2$3$4}'`.dat"
	PROVIDED_SRC=$PROVIDED_CVS
	PW_SRC=$PW_CVS
	PROVIDED_DST=$PROVIDED_SVN
	PW_DST=$PW_SVN
fi
#rm $SCRIPTNAME
echo $SCRIPTNAME would be the output batch script of this shell script. ------------------------

#to get to pscp directory
echo "cd.." >> $SCRIPTNAME



cp $filename temp_$filename
while read line1 < temp_$filename
do
	echo $line1 
	echo "pscp -pw $PW_SRC $PROVIDED_SRC/$line1 $PROVIDED_LOCAL\\$line1" >> $SCRIPTNAME
	echo "pscp -pw $PW_DST $PROVIDED_LOCAL\\$line1 $PROVIDED_DST/$line1" >> $SCRIPTNAME
	sed -i 1d temp_$filename
done
rm temp_$filename

echo "pause" >> $SCRIPTNAME
rm fromsvn.bat fromcvs.bat
if [ $dir -eq 1 ]
then
	cp $SCRIPTNAME fromsvn.bat
else
	cp $SCRIPTNAME fromcvs.bat
fi

