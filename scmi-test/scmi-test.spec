Name: scmi-test
Version: 1.0
Release: r1
Summary: SCMI test applications
License: BSD-3-Clause-Clear
Source0: %{name}-%{version}.tar.gz

BuildRequires: cmake safelinux-modules-uapi-headers glib2-devel
Requires: glib2

%description
Provides applications to test various SCMI protocols on the HGY platform.
These applications use device node exposed by uSCMI interface and send the
scmi request by making ioctl calls. There is a dedicated SCMI channel allocated
for test application. These requests use the same channel.

%prep
%setup -qn %{name}-%{version}

%build
%cmake

%cmake_build

%install
%cmake_install

%files
%{_bindir}/test_scmi_perf
%{_bindir}/test_scmi_power
%{_bindir}/test_scmi
