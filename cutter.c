/*
 * http://ffmpeg.org/doxygen/trunk/index.html
 *
 * Main components
 *
 * Format (Container) - a wrapper, providing sync, metadata and muxing for the streams.
 * Stream - a continuous stream (audio or video) of data over time.
 * Codec - defines how data are enCOded (from Frame to Packet)
 *        and DECoded (from Packet to Frame).
 * Packet - are the data (kind of slices of the stream data) to be decoded as raw frames.
 * Frame - a decoded raw frame (to be encoded or filtered).
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

// Required to create the PNG files
#include <png.h>

// Print out the steps and errors
static void logging(const char *fmt, ...);
// Decode packets into frames
static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame);
// Save a frame into a .png file
static int save_frame_to_png(AVFrame *frame, const char *filename);

// Number of images to create
#define IMAGES_TOTAL 10

int main(int argc, const char *argv[])
{
    if (argc < 2) {
        printf("You need to specify a media file.\n");
        return -1;
    }

    logging("*** Initializing all the containers, codecs and protocols...");

    // AVFormatContext holds the header information from the format (Container)
    // Allocating memory for this component
    // http://ffmpeg.org/doxygen/trunk/structAVFormatContext.html
    AVFormatContext *format_context = avformat_alloc_context();
    if (!format_context) {
        logging("ERROR could not allocate memory for Format Context");
        return -1;
    }

    logging("*** Opening the input file (%s) and loading format (container) header", argv[1]);
    // Open the file and read its header. The codecs are not opened.
    // The function arguments are:
    // AVFormatContext (the component we allocated memory for),
    // url (filename),
    // AVInputFormat (if you pass NULL it'll do the auto detect)
    // and AVDictionary (which are options to the demuxer)
    // http://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html
    if (avformat_open_input(&format_context, argv[1], NULL, NULL) != 0) {
        logging("ERROR could not open the file");
        return -1;
    }

    // now we have access to some information about our file
    // since we read its header we can say what format (container) it's
    // and some other information related to the format itself.
    logging("*** Format: %s, Duration: %lld us, Bitrate: %lld", format_context->iformat->name, format_context->duration, format_context->bit_rate);

    logging("*** Finding stream info from format...");
    // read Packets from the Format to get stream information
    // this function populates pFormatContext->streams
    // (of size equals to pFormatContext->nb_streams)
    // the arguments are:
    // the AVFormatContext
    // and options contains options for codec corresponding to i-th stream.
    // On return each dictionary will be filled with options that were not found.
    // https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html
    if (avformat_find_stream_info(format_context,  NULL) < 0) {
        logging("ERROR could not get the stream info");
        return -1;
    }

    // The component that knows how to enCOde and DECode the stream
    // it's the codec (audio or video)
    // http://ffmpeg.org/doxygen/trunk/structAVCodec.html
    AVCodec *input_codec = NULL;
    // this component describes the properties of a codec used by the stream i
    // https://ffmpeg.org/doxygen/trunk/structAVCodecParameters.html
    AVCodecParameters *input_codec_parameters =  NULL;
    int video_stream_index = -1;

    // Loop though all the streams and print its main information
    for (int i = 0; i < format_context->nb_streams; i++) {
        AVCodecParameters *local_codec_parameters = NULL;
        local_codec_parameters = format_context->streams[i]->codecpar;
        logging("    AVStream->time_base before open coded %d/%d", format_context->streams[i]->time_base.num, format_context->streams[i]->time_base.den);
        logging("    AVStream->r_frame_rate before open coded %d/%d", format_context->streams[i]->r_frame_rate.num, format_context->streams[i]->r_frame_rate.den);
        logging("    AVStream->start_time %" PRId64, format_context->streams[i]->start_time);
        logging("    AVStream->duration %" PRId64, format_context->streams[i]->duration);

        logging("Finding the proper decoder (CODEC)");
        logging("---");

        AVCodec *local_codec = NULL;

        // Finds the registered decoder for a codec ID
        // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html
        local_codec = avcodec_find_decoder(local_codec_parameters->codec_id);

        if (local_codec==NULL) {
            logging("ERROR unsupported codec!");
            // In this example if the codec is not found we just skip it
            continue;
        }

        // When the stream is a video we store its index, codec parameters and codec
        if (local_codec_parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (video_stream_index == -1) {
                video_stream_index = i;
                input_codec = local_codec;
                input_codec_parameters = local_codec_parameters;
            }

            logging("Video Codec: resolution %d x %d", local_codec_parameters->width, local_codec_parameters->height);
        } else if (local_codec_parameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            logging("Audio Codec: %d channels, sample rate %d", local_codec_parameters->channels, local_codec_parameters->sample_rate);
        }

        // Print its name, id and bitrate
        logging("\tCodec %s ID %d bit_rate %lld", local_codec->name, local_codec->id, local_codec_parameters->bit_rate);
    }

    if (video_stream_index == -1) {
        logging("File %s does not contain a video stream!", argv[1]);
        return -1;
    }

    // https://ffmpeg.org/doxygen/trunk/structAVCodecContext.html
    AVCodecContext *codec_context = avcodec_alloc_context3(input_codec);
    if (!codec_context) {
        logging("Failed to allocated memory for AVCodecContext");
        return -1;
    }

    // Fill the codec context based on the values from the supplied codec parameters
    // https://ffmpeg.org/doxygen/trunk/group__lavc__core.html
    if (avcodec_parameters_to_context(codec_context, input_codec_parameters) < 0) {
        logging("Failed to copy codec params to codec context");
        return -1;
    }

    // Initialize the AVCodecContext to use the given AVCodec.
    // https://ffmpeg.org/doxygen/trunk/group__lavc__core.html
    if (avcodec_open2(codec_context, input_codec, NULL) < 0) {
        logging("Failed to open codec through avcodec_open2");
        return -1;
    }

    // https://ffmpeg.org/doxygen/trunk/structAVFrame.html
    AVFrame *input_frame = av_frame_alloc();
    if (!input_frame) {
        logging("Failed to allocate memory for AVFrame");
        return -1;
    }

    // https://ffmpeg.org/doxygen/trunk/structAVPacket.html
    AVPacket *input_packet = av_packet_alloc();
    if (!input_packet) {
        logging("Failed to allocate memory for AVPacket");
        return -1;
    }

    int ret = 0;
    int counter = 0;

    // Fill the Packet with data from the Stream
    // https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html
    while (av_read_frame(format_context, input_packet) >= 0) {
        // If it's the video stream
        if (input_packet->stream_index == video_stream_index) {
            logging("---");
            logging("AVPacket->pts %" PRId64, input_packet->pts);
            ret = decode_packet(input_packet, codec_context, input_frame);
            if (ret < 0)
                break;
            // Stop it, otherwise we'll be saving hundreds of frames
            if (counter > IMAGES_TOTAL)
                break;
            counter++;
        }
        // https://ffmpeg.org/doxygen/trunk/group__lavc__packet.html
        av_packet_unref(input_packet);
    }

    logging("---");
    logging("Releasing all the resources...");

    avformat_close_input(&format_context);
    av_packet_free(&input_packet);
    av_frame_free(&input_frame);
    avcodec_free_context(&codec_context);

    return 0;
}

static void logging(const char *fmt, ...)
{
    va_list args;

    fprintf( stderr, "LOG: " );
    va_start( args, fmt );
    vfprintf( stderr, fmt, args );
    va_end( args );
    fprintf( stderr, "\n" );
}

static int decode_packet(AVPacket *input_packet, AVCodecContext *codec_context, AVFrame *input_frame)
{
    // Supply raw packet data as input to a decoder
    // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html
    int ret = avcodec_send_packet(codec_context, input_packet);

    if (ret < 0) {
        logging("Error while sending a packet to the decoder: %s", av_err2str(ret));
        return ret;
    }

    while (ret >= 0) {
        // Return decoded output data (into a frame) from a decoder
        // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html
        ret = avcodec_receive_frame(codec_context, input_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            logging("Error while receiving a frame from the decoder: %s", av_err2str(ret));
            return ret;
        }

        if (ret >= 0) {
            logging(
                "Frame %d (type=%c, size=%d bytes, format=%d) pts %d key_frame %d [DTS %d]",
                codec_context->frame_number,
                av_get_picture_type_char(input_frame->pict_type),
                input_frame->pkt_size,
                input_frame->format,
                input_frame->pts,
                input_frame->key_frame,
                input_frame->coded_picture_number);

            char frame_filename[1024];
            snprintf(frame_filename, sizeof(frame_filename), "output/%s-%d.png", "frame", codec_context->frame_number);

            // Check if the frame is a planar YUV 4:2:0, 12bpp
            // That is the format of the provided .mp4 file
            // RGB formats will definitely not give a gray image
            // Other YUV image may do so, but untested, so give a warning
            if (input_frame->format != AV_PIX_FMT_YUV420P) {
                logging("Warning: the generated file may not be a grayscale image, but could e.g. be just the R component if the video format is RGB");
            }

            // To create the PNG files, the AVFrame data must be translated from YUV420P format into RGB24
            struct SwsContext *sws_ctx = sws_getContext(
                input_frame->width, input_frame->height, input_frame->format,
                input_frame->width, input_frame->height, AV_PIX_FMT_RGB24,
                SWS_BILINEAR, NULL, NULL, NULL);

            // Allocate a new AVFrame for the output RGB24 image
            AVFrame* rgb_frame = av_frame_alloc();

            // Set the properties of the output AVFrame
            rgb_frame->format = AV_PIX_FMT_RGB24;
            rgb_frame->width = input_frame->width;
            rgb_frame->height = input_frame->height;

            int ret = av_frame_get_buffer(rgb_frame, 0);
            if (ret < 0) {
                logging("Error while preparing RGB frame: %s", av_err2str(ret));
                return ret;
            }

            logging("Transforming frame format from YUV420P into RGB24...");
            ret = sws_scale(sws_ctx, input_frame->data, input_frame->linesize, 0, input_frame->height, rgb_frame->data, rgb_frame->linesize);
            if (ret < 0) {
                logging("Error while translating the frame format from YUV420P into RGB24: %s", av_err2str(ret));
                return ret;
            }

            // save a frame into a .PNG file
            ret = save_frame_to_png(rgb_frame, frame_filename);
            if (ret < 0) {
                fprintf(stderr, "Failed to write PNG file\n");
                return -1;
            }

            av_frame_free(&rgb_frame);
        }
    }

    return 0;
}

// Function to save an AVFrame to a PNG file
int save_frame_to_png(AVFrame *frame, const char *filename)
{
    int ret = 0;

    logging("Creating PNG file -> %s", filename);

    // Open the PNG file for writing
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open file '%s'\n", filename);
        return -1;
    }

    // Create the PNG write struct and info struct
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fprintf(stderr, "Failed to create PNG write struct\n");
        fclose(fp);
        return -1;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        fprintf(stderr, "Failed to create PNG info struct\n");
        png_destroy_write_struct(&png_ptr, NULL);
        fclose(fp);
        return -1;
    }

    // Set up error handling for libpng
    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "Error writing PNG file\n");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return -1;
    }

    // Set the PNG file as the output for libpng
    png_init_io(png_ptr, fp);

    // Set the PNG image attributes
    png_set_IHDR(png_ptr, info_ptr, frame->width, frame->height, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    // Allocate memory for the row pointers and fill them with the AVFrame data
    png_bytep *row_pointers = (png_bytep *) malloc(sizeof(png_bytep) * frame->height);
    for (int y = 0; y < frame->height; y++) {
        row_pointers[y] = (png_bytep) (frame->data[0] + y * frame->linesize[0]);
    }

    // Write the PNG file
    png_set_rows(png_ptr, info_ptr, row_pointers);
    png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

    // Clean up
    free(row_pointers);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);

    return ret;
}
