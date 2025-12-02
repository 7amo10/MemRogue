%define name memrogue
%define version 1.0.0
%define release 1

Name:           %{name}
Version:        %{version}
Release:        %{release}%{?dist}
Summary:        Lightweight memory debugging tool for C/C++ applications

License:        MIT
URL:            https://github.com/7amo10/MemRogue
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.15
BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  make

Requires:       glibc >= 2.17

%description
MemRogue is a powerful memory debugging tool that helps detect memory
leaks, double frees, and invalid frees in C/C++ applications.

Features:
- Memory leak detection with stack traces
- Double free detection with detailed error reporting
- Invalid free detection
- JSON and CSV report format support
- Minimal runtime overhead
- Easy LD_PRELOAD integration

%package devel
Summary:        Development files for MemRogue
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description devel
This package contains header files and development libraries
for building applications that use the MemRogue API directly.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DMEMROGUE_BUILD_TESTS=OFF \
    -DMEMROGUE_BUILD_EXAMPLES=OFF \
    -DMEMROGUE_BUILD_BENCHMARKS=OFF
%cmake_build

%install
%cmake_install

%files
%license LICENSE
%doc README.md
%{_bindir}/memrogue
%{_bindir}/memrogue-report
%{_libdir}/libmemrogue_intercept.so.*
%{_datadir}/memrogue/
%{_docdir}/memrogue/

%files devel
%{_includedir}/memrogue/
%{_libdir}/libmemrogue_core.a
%{_libdir}/libmemrogue_intercept.so
%{_libdir}/pkgconfig/memrogue.pc
%{_libdir}/cmake/MemRogue/

%changelog
* Tue Dec 02 2025 MemRogue Team <a8087027@gmail.com> - 1.0.0-1
- Initial release
- Memory leak detection with configurable stack trace depth
- Double free detection with detailed error reporting
- Invalid free detection
- JSON and CSV report format support
- Easy-to-use wrapper script for LD_PRELOAD integration
