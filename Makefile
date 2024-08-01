CXXFLAGS = $(shell pkg-config --cflags gstreamer-1.0 gstreamer-video-1.0 opencv4)
LIBS = $(shell pkg-config --libs gstreamer-1.0 gstreamer-video-1.0 opencv4)

libgstubntmask.so: ubnt_mask.cpp
	$(CXX) -shared -o $@ $< $(CXXFLAGS) $(LIBS) -fPIC
