Name: early-service-infra
Version: 1.0
Release: r0
Summary: systemd target and example for early services
License: BSD-3-Clause-Clear
Source0: %{name}-%{version}.tar.gz
BuildRequires: cmake gcc-c++ systemd systemd-rpm-macros pkgconfig pkgconfig(bootkpi-logging)
%{?systemd_requires}

%description
This systemd target is a synchronization point for all early services. Early services shall be configured to start Before=early-services.target

%package example-service
Summary: example of a systemd early service
Requires: %{name} = %{version}-%{release}

%description example-service
This early services example shows how to start a systemd service very early in the
boot, and order it before early-services.target. The default dependencies are
disabled, allowing the service to start before systemd's basic.target

%prep
%setup -qn %{name}

%build
%cmake
%cmake_build

%install
%cmake_install

%post
systemctl enable early-services.target

%preun
%systemd_preun early-services.target

%postun
%systemd_postun early-services.target

%files
%{_unitdir}/early-services.target

%files example-service
%{_unitdir}/early-service-example.service
%{_bindir}/early-service-example
