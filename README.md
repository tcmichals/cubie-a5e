cd buildroot/
make BR2_EXTERNAL=../project-cubie-a5e cubie_a5e_defconfig
make
make BR2_EXTERNAL=../project-cubie-a5e cubie_a5e_defconfig O=$PWD -C ../buildroot/

