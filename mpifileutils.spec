Name:		mpifileutils
Version:	0.10.1
Release:	1%{?relval}%{?dist}
Summary:	File utilities designed for scalability and performance.

Group:		System Environment/Libraries
License:	BSD
URL:		https://hpc.github.io/mpifileutils

#Source:	https://github.com/hpc/mpifileutils/releases/download/v#{version}/#{name}-#{version}.tar.gz
Source0:	https://github.com/daos-stack/%{name}/releases/download/%{shortcommit0}/%{name}-%{version}.tar.gz

BuildRequires: libcircle-openmpi-devel dtcmp-devel
BuildRequires: libarchive-devel openssl-devel bzip2-devel
BuildRequires: openmpi3-devel pkgconfig automake
BuildRequires: libattr-devel autoconf%{?el6:268}
BuildRequires: daos-devel

%if (0%{?rhel} <= 7)
%global cmake cmake3
%else
%global cmake
%endif
BuildRequires: %{cmake}

%description
File utilities designed for scalability and performance.

%prep
%setup -q

%build
#topdir=`pwd`
#installdir=$topdir/install
%{_openmpi3_load}

export PKG_CONFIG_PATH="$PKG_CONFIG_PATH:$MPI_LIB/pkgconfig"
export CC=mpicc

%{cmake} ./ -DDTCMP_INCLUDE_DIRS=/usr/include/openmpi3-x86_64 \
            -DWITH_DTCMP_PREFIX=/usr/lib64/openmpi3 \
            -DLibCircle_INCLUDE_DIRS=/usr/include/openmpi3-x86_64 \
            -DWITH_LibCircle_PREFIX=/usr/lib64/openmpi3 \
            -DWITH_CART_PREFIX=/usr \
            -DWITH_DAOS_PREFIX=/usr \
            -DENABLE_DAOS=ON \
            -DCMAKE_INSTALL_PREFIX=/usr
make

%install
rm -rf %{buildroot}
make install DESTDIR=%{buildroot}
mkdir %{buildroot}%{_libdir}/openmpi3
mkdir %{buildroot}%{_libdir}/openmpi3/bin
mkdir %{buildroot}%{_includedir}/openmpi3-%{_arch}
mkdir %{buildroot}%{_libdir}/openmpi3/lib
mkdir %{buildroot}%{_mandir}/openmpi3-%{_arch}
mkdir %{buildroot}%{_mandir}/openmpi3-%{_arch}/man1
mv %{buildroot}%{_bindir}/* %{buildroot}%{_libdir}/openmpi3/bin/
mv %{buildroot}%{_includedir}/*.h %{buildroot}%{_includedir}/openmpi3-%{_arch}/
mv %{buildroot}%{_libdir}/*.so* %{buildroot}/%{_libdir}/openmpi3/lib/
mv %{buildroot}%{_libdir}/*.a* %{buildroot}/%{_libdir}/openmpi3/lib/
mv %{buildroot}%{_mandir}/man1/* %{buildroot}/%{_mandir}/openmpi3-%_arch/man1/

%files
%defattr(-,root,root,-)
#{_bindir}/*
#{_includedir}/*
#{_libdir}/*
#{_mandir}/man1/*
%{_libdir}/openmpi3/bin/*
%{_includedir}/openmpi3-%_arch/*
%{_libdir}/openmpi3/lib
%{_mandir}/openmpi3-%_arch/man1/*


%changelog
* Wed Sep 23 2020 John E. Malmberg <john.e.malmberg@intel.com> - 0.1.0-1
- Branch for testing daos-stack fork.
- Updates from  https://copr-dist-git.fedorainfracloud.org/cgit/loveshack/livhpc
- Set up for packaging pre-release branches with release version based on git.

