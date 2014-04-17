%define major 3
%define minor 0
%define patchlevel 1

Name:       tel-plugin-imcmodem
Version:        %{major}.%{minor}.%{patchlevel}
Release:    1
License:    Apache-2.0
Summary:        Telephony Plug-in for AT communication with IMC modem (Modem Interface Plug-in)
Group:          System/Libraries
Source0:    tel-plugin-imcmodem-%{version}.tar.gz
Source1001: 	tel-plugin-imcmodem.manifest
BuildRequires:  cmake
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(tcore)
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description
imcmodem plugin for telephony

%prep
%setup -q
cp %{SOURCE1001} .

%build
%cmake .
make %{?jobs:-j%jobs}

%post
/sbin/ldconfig

%postun -p /sbin/ldconfig

%install
%make_install
mkdir -p %{buildroot}/usr/share/license
cp LICENSE %{buildroot}/usr/share/license/%{name}

%files
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_libdir}/telephony/plugins/*
/usr/share/license/%{name}
