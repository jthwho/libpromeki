/**
 * @file      proav/mediaport.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/sharedptr.h>
#include <promeki/core/string.h>
#include <promeki/proav/audiodesc.h>
#include <promeki/proav/videodesc.h>
#include <promeki/proav/imagedesc.h>
#include <promeki/proav/encodeddesc.h>

PROMEKI_NAMESPACE_BEGIN

class MediaNode;

/**
 * @brief Describes a node's input or output connection point.
 * @ingroup proav_pipeline
 *
 * Port types define what media a port carries. A Frame port carries the full
 * Frame (image + audio + metadata). Image and Audio ports carry their
 * respective sub-frame data plus metadata. This allows nodes to work at the
 * level of abstraction they need.
 */
class MediaPort {
        PROMEKI_SHARED_FINAL(MediaPort)
        public:
                /** @brief Shared pointer type for MediaPort. */
                using Ptr = SharedPtr<MediaPort>;

                /** @brief Plain value list of MediaPort objects. */
                using List = promeki::List<MediaPort>;

                /** @brief List of shared pointers to MediaPort. */
                using PtrList = promeki::List<Ptr>;

                /** @brief Port direction. */
                enum Direction {
                        Input,  ///< @brief Input port (receives data).
                        Output  ///< @brief Output port (produces data).
                };

                /**
                 * @brief Media type carried by this port.
                 *
                 * Determines what kind of media data flows through the port and
                 * which descriptor fields are meaningful.
                 */
                enum MediaType {
                        Frame,          ///< @brief Full frame (image + audio + metadata). Described by VideoDesc + AudioDesc.
                        Image,          ///< @brief Image + metadata. Described by ImageDesc.
                        Audio,          ///< @brief Audio + metadata. Described by AudioDesc.
                        Encoded         ///< @brief Compressed/encoded data + metadata. Described by EncodedDesc.
                };

                /** @brief Constructs a default (unnamed, Frame/Input) port. */
                MediaPort() = default;

                /**
                 * @brief Constructs a port with the given name, direction, and media type.
                 * @param name      Human-readable port name.
                 * @param direction Port direction (Input or Output).
                 * @param mediaType The type of media this port carries.
                 */
                MediaPort(const String &name, Direction direction, MediaType mediaType) :
                        _name(name), _direction(direction), _mediaType(mediaType) { }

                /** @brief Returns the port name. */
                const String &name() const { return _name; }

                /**
                 * @brief Sets the port name.
                 * @param name The new port name.
                 */
                void setName(const String &name) { _name = name; return; }

                /** @brief Returns the port direction. */
                Direction direction() const { return _direction; }

                /** @brief Returns the media type carried by this port. */
                MediaType mediaType() const { return _mediaType; }

                /**
                 * @brief Returns the audio description.
                 *
                 * Valid when mediaType is Audio or Frame.
                 */
                const AudioDesc &audioDesc() const { return _audioDesc; }

                /**
                 * @brief Sets the audio description.
                 * @param desc The audio format description.
                 */
                void setAudioDesc(const AudioDesc &desc) { _audioDesc = desc; return; }

                /**
                 * @brief Returns the video description.
                 *
                 * Valid when mediaType is Frame.
                 */
                const VideoDesc &videoDesc() const { return _videoDesc; }

                /**
                 * @brief Sets the video description.
                 * @param desc The video format description.
                 */
                void setVideoDesc(const VideoDesc &desc) { _videoDesc = desc; return; }

                /**
                 * @brief Returns the image description.
                 *
                 * Valid when mediaType is Image or Frame.
                 */
                const ImageDesc &imageDesc() const { return _imageDesc; }

                /**
                 * @brief Sets the image description.
                 * @param desc The image format description.
                 */
                void setImageDesc(const ImageDesc &desc) { _imageDesc = desc; return; }

                /**
                 * @brief Returns the encoded description.
                 *
                 * Valid when mediaType is Encoded.
                 */
                const EncodedDesc &encodedDesc() const { return _encodedDesc; }

                /**
                 * @brief Sets the encoded description.
                 * @param desc The encoded format description.
                 */
                void setEncodedDesc(const EncodedDesc &desc) { _encodedDesc = desc; return; }

                /** @brief Returns true if this port is currently connected. */
                bool isConnected() const { return _connected; }

                /**
                 * @brief Sets the connected state of this port.
                 * @param connected The new connected state.
                 */
                void setConnected(bool connected) { _connected = connected; return; }

                /** @brief Returns the node that owns this port, or nullptr. */
                MediaNode *node() const { return _node; }

                /**
                 * @brief Sets the owning node.
                 * @param node The node that owns this port.
                 */
                void setNode(MediaNode *node) { _node = node; return; }

                /**
                 * @brief Tests whether this port is compatible with another port for connection.
                 *
                 * Compatibility rules:
                 * - Same media type: check descriptor match
                 * - Frame output to Image input: compatible (image extracted from frame)
                 * - Frame output to Audio input: compatible (audio extracted from frame)
                 * - Image/Audio to Frame: not compatible
                 * - Encoded only compatible with Encoded
                 *
                 * @param other The port to check compatibility with.
                 * @return true if the ports can be connected.
                 */
                bool isCompatible(const MediaPort &other) const;

        private:
                String          _name;
                Direction       _direction = Input;
                MediaType       _mediaType = Frame;
                AudioDesc       _audioDesc;
                VideoDesc       _videoDesc;
                ImageDesc       _imageDesc;
                EncodedDesc     _encodedDesc;
                bool            _connected = false;
                MediaNode       *_node = nullptr;
};

PROMEKI_NAMESPACE_END
