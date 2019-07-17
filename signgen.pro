TEMPLATE = app
CONFIG += console c++14 sdk_no_version_check
CONFIG -= app_bundle
CONFIG -= qt

macx {
_BOOST_PATH = /usr/local/opt/boost

INCLUDEPATH += \
	"$${_BOOST_PATH}/include/"

LIBS += \
	-L"$${_BOOST_PATH}/lib"

LIBS += \
	-lboost_filesystem-mt \
	-lboost_system-mt \
	-lboost_iostreams-mt \
	-lboost_thread-mt \
	-lboost_chrono-mt \
	-lboost_date_time-mt \
	-lboost_timer-mt \
	-lpthread
}

unix:!macx {
LIBS += \
	-lboost_filesystem \
	-lboost_system \
	-lboost_iostreams \
	-lboost_thread \
	-lboost_chrono \
	-lboost_date_time \
	-lboost_timer \
	-lpthread
}
win32 {
BOOST_DIR = $$(BOOST_DIR) # is environment variable where your `BOOST` location

isEmpty(BOOST_DIR) {
message("BOOST DIRECTORY" not detected...)
}

# Additional includes
INCLUDEPATH +=	\
		$$BOOST_DIR/

LIBS += \
	-L$$BOOST_DIR/stage/x64/lib
}


SOURCES += \
        main.cpp
