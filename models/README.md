# Face Embedding Model

`face_recognition_sface_2021dec.onnx` is the OpenCV Zoo SFace model used by
the default application. It accepts an aligned 112 x 112 BGR face and returns
a 128-dimensional feature vector. The application L2-normalizes that vector
before publishing it.

- Source: https://github.com/opencv/opencv_zoo/tree/main/models/face_recognition_sface
- Download mirror: https://huggingface.co/opencv/face_recognition_sface
- Upstream project: https://github.com/opencv/opencv
- Model filename: `face_recognition_sface_2021dec.onnx`
- SHA-256: `0ba9fbfa01b5270c96627c4ef784da859931e02f04419c829e83484087c34e79`
- License: Apache License 2.0; see `LICENSE` in this directory.

Keep the license and model metadata with the model when redistributing the
repository. CMake verifies the bundled model checksum, and the application
validates its output contract during startup rather than running without feature
extraction.
