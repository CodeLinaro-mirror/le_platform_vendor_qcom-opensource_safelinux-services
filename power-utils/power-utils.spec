Name: power-utils
Version: 1.0
Release: r0
Summary: userspace power utilities
License: BSD-3-Clause-Clear
URL: https://www.codelinaro.org/
Source0: %{name}-%{version}.tar.gz
BuildRequires: cmake gcc gcc-c++ glibc-devel systemd-rpm-macros pkgconfig pkgconfig(libsystemd)
%{?systemd_requires}

%description
Utilities to support userspace suspend and shutdown

%package devel
Summary: userspace power utils - development files
Requires: %{name} = %{version}-%{release}

%description devel
Development files for power utils, including header files, libraries, and pkgconfig
%prep
%setup -n %{name}

%build
%cmake
%cmake_build

%install
%cmake_install

%post
systemctl enable check-inhibitors.service
systemctl enable failure-resume.service
systemctl enable trigger-resume.service
systemctl enable sleep-apps.target
systemctl enable sleep-bounds.target
systemctl enable sleep-drivers.target
systemctl enable failure-resume.service

%preun
%systemd_preun check-inhibitors.service
%systemd_preun failure-resume.service
%systemd_preun trigger-resume.service
%systemd_preun sleep-apps.target
%systemd_preun sleep-bounds.target
%systemd_preun sleep-drivers.target
%systemd_preun failure-resume.service

%postun
%systemd_postun_with_restart check-inhibitors.service
%systemd_postun failure-resume.service
%systemd_postun trigger-resume.service
%systemd_postun sleep-apps.target
%systemd_postun sleep-bounds.target
%systemd_postun sleep-drivers.target
%systemd_postun failure-resume.service

%files
%{_bindir}/check-inhibitors
%{_unitdir}/check-inhibitors.service
%{_bindir}/pm-notify
%{_unitdir}/*.service
%{_unitdir}/*.target
%{_unitdir}/sleep.target.d/30-qcom-override.conf
%{_libdir}/libpm-client.so
%{_usr}/lib/tmpfiles-early.d/qcom_pm.conf

%files devel
%{_includedir}/*.h
%{_libdir}/libpm-client.so.*
%{_libdir}/pkgconfig/pm-client.pc
