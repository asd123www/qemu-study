sudo apt update
sudo apt install cmake -y

# install hiredis
git clone https://github.com/redis/hiredis.git
cd hiredis
make
make install
cd ..

# install redis-plus-plus
git clone https://github.com/sewenew/redis-plus-plus.git
cd redis-plus-plus
mkdir build
cd build
cmake ..
make
make install
cd ..

# load libredis++.so.1
sudo ldconfig