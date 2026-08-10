Name: scm-user-intf-test
Version: 1.0
Release: r1
Summary: Unit tests for the scm_user_intf driver (/dev/scmnode)
License: BSD-3-Clause-Clear
Source0: %{name}-%{version}.tar.gz

BuildRequires: cmake safelinux-modules-uapi-headers

%description
Userspace unit tests that exercise the scm_user_intf kernel driver
via the /dev/scmnode character device and the SCM_HAND_SHAKE_IOCTL
interface.  Covers device open/close, ioctl command validation,
service/command whitelist enforcement, argument-count overflow, and
functional read-only SCM calls into TrustZone.

%prep
%setup -qn %{name}-%{version}

%build
%cmake

%cmake_build

%install
%cmake_install

%files
%{_bindir}/test_scm_user_intf
