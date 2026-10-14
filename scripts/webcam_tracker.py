#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Script auxiliar de Rastreamento Facial, Postura Calibrada, Gestos Manuais (Shhh, Joinha, Vitória, Palma),
Multi-Faces, Luz Ambiente Real e Visor PIP RGB565/CLAHE em Tempo Real para o Emulador PC (SDL2).
- Espelha a mesma arquitetura de percepção embarcada do M5Stack CoreS3 (GC0308 + LTR-553ALS).
- Compatível tanto com OpenCV 5.x (cv2.FaceDetectorYN / YuNet ONNX) quanto com OpenCV 4.x (CascadeClassifier).
- Atualiza continuamente 'face_track.txt' com telemetria visual estendida (26 campos na Linha 1):
  Linha 1: timestamp face_detected cx cy tilt_deg slouch close cpu ram smile looking_down
           hand_wave git_dirty face_count sec_x sec_y yaw_deg face_w is_yawning
           object_close ambient_lux scene_change_pct
           gesture_shhh gesture_thumbs_up gesture_victory gesture_open_palm
  Linha 2: active_window_title
- Também gera buffers PIP 64x48 e 160x120 tanto em Grayscale CLAHE ('cam_pip.raw', 'cam_pip_hd.raw')
  quanto em Cor Real 16-bit RGB565 ('cam_pip_rgb565.raw', 'cam_pip_hd_rgb565.raw').
"""

import ctypes
from ctypes import wintypes
import math
import os
import sys
import time
import urllib.request
import cv2
import numpy as np

FRAME_WIDTH = 320
FRAME_HEIGHT = 240
PIP_WIDTH = 64
PIP_HEIGHT = 48
PIP_HD_WIDTH = 160
PIP_HD_HEIGHT = 120
SNAP_UPDATE_INTERVAL_SEC = 1.2
PIP_UPDATE_INTERVAL_SEC = 0.16
TELEMETRY_UPDATE_INTERVAL_SEC = 1.0

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
FACE_TRACK_FILE = os.path.join(PROJECT_ROOT, "face_track.txt")
CAM_SNAP_FILE = os.path.join(PROJECT_ROOT, "cam_snap.jpg")
CAM_SNAP_TMP_FILE = os.path.join(PROJECT_ROOT, "cam_snap_tmp.jpg")
CAM_PIP_FILE = os.path.join(PROJECT_ROOT, "cam_pip.raw")
CAM_PIP_HD_FILE = os.path.join(PROJECT_ROOT, "cam_pip_hd.raw")
CAM_PIP_RGB565_FILE = os.path.join(PROJECT_ROOT, "cam_pip_rgb565.raw")
CAM_PIP_HD_RGB565_FILE = os.path.join(PROJECT_ROOT, "cam_pip_hd_rgb565.raw")
YUNET_MODEL_PATH = os.path.join(SCRIPT_DIR, "face_detection_yunet_2023mar.onnx")
YUNET_MODEL_URL = (
    "https://github.com/opencv/opencv_zoo/raw/main/models/"
    "face_detection_yunet/face_detection_yunet_2023mar.onnx"
)
PALM_MODEL_PATH = os.path.join(SCRIPT_DIR, "palm_detection_mediapipe_2023feb.onnx")
PALM_MODEL_URL = (
    "https://github.com/opencv/opencv_zoo/raw/main/models/"
    "palm_detection_mediapipe/palm_detection_mediapipe_2023feb.onnx"
)
HANDPOSE_MODEL_PATH = os.path.join(SCRIPT_DIR, "handpose_estimation_mediapipe_2023feb.onnx")
HANDPOSE_MODEL_URL = (
    "https://github.com/opencv/opencv_zoo/raw/main/models/"
    "handpose_estimation_mediapipe/handpose_estimation_mediapipe_2023feb.onnx"
)


# =============================================================================
# Coleta Leve de Telemetria do Sistema Host (CPU %, RAM % e Janela Ativa)
# =============================================================================
class MemoryStatusEx(ctypes.Structure):
    _fields_ = [
        ("dwLength", wintypes.DWORD),
        ("dwMemoryLoad", wintypes.DWORD),
        ("ullTotalPhys", ctypes.c_ulonglong),
        ("ullAvailPhys", ctypes.c_ulonglong),
        ("ullTotalPageFile", ctypes.c_ulonglong),
        ("ullAvailPageFile", ctypes.c_ulonglong),
        ("ullTotalVirtual", ctypes.c_ulonglong),
        ("ullAvailVirtual", ctypes.c_ulonglong),
        ("sullAvailExtendedVirtual", ctypes.c_ulonglong),
    ]


class FileTime(ctypes.Structure):
    _fields_ = [
        ("dwLowDateTime", wintypes.DWORD),
        ("dwHighDateTime", wintypes.DWORD),
    ]


_prev_idle_time = 0
_prev_kernel_time = 0
_prev_user_time = 0
_last_git_check_time = 0.0
_cached_git_dirty = 0


def _filetime_to_u64(ft: FileTime) -> int:
    return (int(ft.dwHighDateTime) << 32) | int(ft.dwLowDateTime)


def query_git_dirty_files() -> int:
    """Conta rapidamente arquivos modificados no repositório Git local sem bloquear o loop."""
    global _last_git_check_time, _cached_git_dirty
    now_t = time.time()
    if (now_t - _last_git_check_time) < 6.0:
        return _cached_git_dirty
    _last_git_check_time = now_t
    try:
        import subprocess
        res = subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=PROJECT_ROOT,
            capture_output=True,
            text=True,
            timeout=0.35
        )
        if res.returncode == 0:
            lines = [ln for ln in res.stdout.splitlines() if ln.strip()]
            _cached_git_dirty = min(99, len(lines))
    except Exception:
        pass
    return _cached_git_dirty


def query_host_pc_telemetry():
    """Retorna (cpu_pct: int, ram_pct: int, active_window_short: str, git_dirty: int) usando Win32 ctypes."""
    global _prev_idle_time, _prev_kernel_time, _prev_user_time
    cpu_pct = 18
    ram_pct = 45
    win_title = "Desktop"
    git_dirty = query_git_dirty_files()

    if not sys.platform.startswith("win"):
        return cpu_pct, ram_pct, win_title, git_dirty

    try:
        mem_stat = MemoryStatusEx()
        mem_stat.dwLength = ctypes.sizeof(MemoryStatusEx)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(mem_stat)):
            ram_pct = max(0, min(100, int(mem_stat.dwMemoryLoad)))

        idle_ft = FileTime()
        kernel_ft = FileTime()
        user_ft = FileTime()
        if ctypes.windll.kernel32.GetSystemTimes(
            ctypes.byref(idle_ft), ctypes.byref(kernel_ft), ctypes.byref(user_ft)
        ):
            idle_now = _filetime_to_u64(idle_ft)
            kernel_now = _filetime_to_u64(kernel_ft)
            user_now = _filetime_to_u64(user_ft)

            idle_diff = idle_now - _prev_idle_time
            total_diff = (kernel_now - _prev_kernel_time) + (user_now - _prev_user_time)
            _prev_idle_time = idle_now
            _prev_kernel_time = kernel_now
            _prev_user_time = user_now

            if total_diff > 0 and _prev_kernel_time > 0:
                busy = max(0, total_diff - idle_diff)
                cpu_pct = max(1, min(100, int(round((busy * 100.0) / total_diff))))

        hwnd = ctypes.windll.user32.GetForegroundWindow()
        if hwnd:
            length = ctypes.windll.user32.GetWindowTextLengthW(hwnd)
            if length > 0:
                buf = ctypes.create_unicode_buffer(length + 1)
                ctypes.windll.user32.GetWindowTextW(hwnd, buf, length + 1)
                raw_title = buf.value.strip()
                if raw_title:
                    parts = [p.strip() for p in raw_title.split(" - ") if p.strip()]
                    if len(parts) >= 2:
                        short_title = f"{parts[-2][:22]} - {parts[-1][:22]}"
                    else:
                        short_title = raw_title
                    clean_chars = [
                        ch if (32 <= ord(ch) <= 126) else " "
                        for ch in short_title
                    ]
                    win_title = "".join(clean_chars).strip()[:48] or "Desktop"
    except Exception:
        pass

    return cpu_pct, ram_pct, win_title, git_dirty


def ensure_yunet_model() -> bool:
    """Garante que o modelo leve YuNet ONNX (227 KB) exista localmente para o OpenCV 5.x."""
    if os.path.exists(YUNET_MODEL_PATH) and os.path.getsize(YUNET_MODEL_PATH) > 100000:
        return True
    try:
        urllib.request.urlretrieve(YUNET_MODEL_URL, YUNET_MODEL_PATH)
        return os.path.exists(YUNET_MODEL_PATH) and os.path.getsize(YUNET_MODEL_PATH) > 100000
    except Exception:
        return False


def bgr_to_rgb565_bytes(frame_bgr: np.ndarray) -> bytes:
    """Converte um frame BGR (OpenCV) para buffer binário RGB565 little-endian de 16 bits."""
    b = frame_bgr[:, :, 0].astype(np.uint16) >> 3
    g = frame_bgr[:, :, 1].astype(np.uint16) >> 2
    r = frame_bgr[:, :, 2].astype(np.uint16) >> 3
    rgb565 = (r << 11) | (g << 5) | b
    return rgb565.astype("<u2").tobytes()


def capture_single_snapshot(output_path: str = CAM_SNAP_FILE) -> bool:
    """Captura um único frame 320x240 da webcam padrão e salva em JPEG de alta nitidez."""
    cap = cv2.VideoCapture(0, cv2.CAP_DSHOW if sys.platform.startswith("win") else cv2.CAP_ANY)
    if not cap.isOpened():
        cap = cv2.VideoCapture(0)
    if not cap.isOpened():
        return False

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT)

    ok = False
    frame = None
    for _ in range(4):
        ok, frame = cap.read()
        if ok and frame is not None:
            break
        time.sleep(0.03)

    cap.release()
    if not ok or frame is None:
        return False

    resized = cv2.resize(frame, (FRAME_WIDTH, FRAME_HEIGHT))
    cv2.imwrite(output_path, resized, [int(cv2.IMWRITE_JPEG_QUALITY), 85])
    return os.path.exists(output_path)


def create_face_detector():
    """Cria detector facial compatível com OpenCV 5.x (FaceDetectorYN) ou OpenCV 4.x (CascadeClassifier)."""
    if hasattr(cv2, "FaceDetectorYN") and ensure_yunet_model():
        try:
            yunet = cv2.FaceDetectorYN.create(
                YUNET_MODEL_PATH,
                "",
                (FRAME_WIDTH, FRAME_HEIGHT),
                0.52,
                0.3,
                5000
            )
            return ("yunet", yunet)
        except Exception:
            pass

    if hasattr(cv2, "CascadeClassifier"):
        cascade_path = os.path.join(cv2.data.haarcascades, "haarcascade_frontalface_default.xml")
        if os.path.exists(cascade_path):
            return ("haar", cv2.CascadeClassifier(cascade_path))

    return ("none", None)


def map_cam_x_to_screen(raw_cx: int) -> int:
    """Espelha X horizontalmente e amplifica a faixa central para cobrir [0, 319]."""
    mirrored_x = FRAME_WIDTH - raw_cx
    norm_dx = (mirrored_x - (FRAME_WIDTH * 0.5)) * 1.45
    mapped_x = int(round((FRAME_WIDTH * 0.5) + norm_dx))
    return max(0, min(FRAME_WIDTH - 1, mapped_x))


def map_cam_y_to_screen(raw_cy: int) -> int:
    """Amplifica suavemente a faixa vertical para cobrir [0, 239]."""
    norm_dy = (raw_cy - (FRAME_HEIGHT * 0.5)) * 1.45
    mapped_y = int(round((FRAME_HEIGHT * 0.5) + norm_dy))
    return max(0, min(FRAME_HEIGHT - 1, mapped_y))


_hand_engine_singleton = None


def _build_palm_anchors() -> np.ndarray:
    """Gera as 2016 âncoras SSD do modelo MediaPipe Palm Detection (192x192)."""
    g24_y, g24_x = np.meshgrid((np.arange(24) + 0.5) / 24.0, (np.arange(24) + 0.5) / 24.0, indexing="ij")
    a24 = np.repeat(np.stack([g24_x.ravel(), g24_y.ravel()], axis=1), 2, axis=0)
    g12_y, g12_x = np.meshgrid((np.arange(12) + 0.5) / 12.0, (np.arange(12) + 0.5) / 12.0, indexing="ij")
    a12 = np.repeat(np.stack([g12_x.ravel(), g12_y.ravel()], axis=1), 6, axis=0)
    return np.concatenate([a24, a12], axis=0).astype(np.float32)


class MediaPipeHandEngineONNX:
    """
    Motor neural leve de 21 Keypoints 3D da Mão (MediaPipe Palm Detection + HandPose ONNX via ONNXRuntime).
    Roda em ~11ms na CPU e distingue com precisão cirúrgica:
      - 👍 Joinha (Thumbs Up): polegar apontando para cima + 4 dedos fechados no punho
      - ✌️ Vitória (V-Sign): indicador e médio estendidos em V + anelar e mínimo fechados
      - ✋ Palma Aberta (Open Palm): 4 dedos estendidos abertos
      - 🤫 Silêncio (Shhh): apenas o indicador estendido verticalmente cruzando a região da boca/nariz
    """

    def __init__(self):
        self.ready = False
        self.palm_sess = None
        self.hand_sess = None
        self.anchors = _build_palm_anchors()
        self.palm_input_size = np.array([192, 192], dtype=np.int32)
        self.hand_input_size = np.array([224, 224], dtype=np.int32)
        try:
            import onnxruntime as ort
            if not (os.path.exists(PALM_MODEL_PATH) and os.path.getsize(PALM_MODEL_PATH) > 1000000):
                urllib.request.urlretrieve(PALM_MODEL_URL, PALM_MODEL_PATH)
            if not (os.path.exists(HANDPOSE_MODEL_PATH) and os.path.getsize(HANDPOSE_MODEL_PATH) > 1000000):
                urllib.request.urlretrieve(HANDPOSE_MODEL_URL, HANDPOSE_MODEL_PATH)
            if os.path.exists(PALM_MODEL_PATH) and os.path.exists(HANDPOSE_MODEL_PATH):
                sess_opts = ort.SessionOptions()
                sess_opts.intra_op_num_threads = 2
                self.palm_sess = ort.InferenceSession(
                    PALM_MODEL_PATH, sess_options=sess_opts, providers=["CPUExecutionProvider"]
                )
                self.hand_sess = ort.InferenceSession(
                    HANDPOSE_MODEL_PATH, sess_options=sess_opts, providers=["CPUExecutionProvider"]
                )
                self.ready = True
        except Exception:
            self.ready = False

    def _detect_palms(self, frame_bgr: np.ndarray, score_thresh: float = 0.46, nms_thresh: float = 0.30):
        h, w = frame_bgr.shape[:2]
        pad_bias = np.array([0.0, 0.0], dtype=np.float32)
        ratio = float(min(self.palm_input_size[0] / float(h), self.palm_input_size[1] / float(w)))
        ratio_h = int(round(h * ratio))
        ratio_w = int(round(w * ratio))
        resized = cv2.resize(frame_bgr, (ratio_w, ratio_h))
        pad_h = int(self.palm_input_size[0] - ratio_h)
        pad_w = int(self.palm_input_size[1] - ratio_w)
        left = pad_w // 2
        top = pad_h // 2
        right = pad_w - left
        bottom = pad_h - top
        pad_bias[0] = float(left)
        pad_bias[1] = float(top)
        padded = cv2.copyMakeBorder(resized, top, bottom, left, right, cv2.BORDER_CONSTANT, None, (0, 0, 0))
        rgb = cv2.cvtColor(padded, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
        blob = rgb[np.newaxis, :, :, :]

        out_reg, out_clf = self.palm_sess.run(None, {"input_1": blob})
        raw_scores = np.clip(out_clf[0, :, 0].astype(np.float64), -60.0, 60.0)
        scores = 1.0 / (1.0 + np.exp(-raw_scores))

        mask = scores >= score_thresh
        if not np.any(mask):
            return []

        box_delta = out_reg[0, mask, 0:4]
        lm_delta = out_reg[0, mask, 4:18]
        sel_anchors = self.anchors[mask]
        sel_scores = scores[mask]

        scale = float(max(w, h))
        pad_px = (pad_bias / ratio).astype(np.float32)

        cxy = box_delta[:, :2] / self.palm_input_size + sel_anchors
        wh = box_delta[:, 2:4] / self.palm_input_size
        xy1 = (cxy - wh * 0.5) * scale - pad_px
        xy2 = (cxy + wh * 0.5) * scale - pad_px
        boxes_xyxy = np.concatenate([xy1, xy2], axis=1)
        boxes_xywh = np.concatenate([xy1, xy2 - xy1], axis=1)

        keep = cv2.dnn.NMSBoxes(boxes_xywh.tolist(), sel_scores.tolist(), score_thresh, nms_thresh, top_k=3)
        if len(keep) == 0:
            return []

        keep_idx = np.array(keep).ravel()
        palms = []
        for idx in keep_idx[:2]:
            lms = lm_delta[idx].reshape(7, 2) / self.palm_input_size + sel_anchors[idx]
            lms = lms * scale - pad_px
            palms.append((boxes_xyxy[idx].reshape(2, 2), lms, float(sel_scores[idx])))
        return palms

    def _crop_and_pad_from_palm(self, image: np.ndarray, palm_bbox: np.ndarray, for_rotation: bool = False):
        wh_palm = palm_bbox[1] - palm_bbox[0]
        shift_vec = np.array([0.0, 0.0] if for_rotation else [0.0, -0.4], dtype=np.float32)
        shifted_bbox = palm_bbox + shift_vec * wh_palm
        center_palm = np.sum(shifted_bbox, axis=0) * 0.5
        wh_shifted = shifted_bbox[1] - shifted_bbox[0]
        enlarge_scale = 4.0 if for_rotation else 3.0
        new_half = (wh_shifted * enlarge_scale) * 0.5
        crop_bbox = np.array([center_palm - new_half, center_palm + new_half], dtype=np.int32)
        crop_bbox[:, 0] = np.clip(crop_bbox[:, 0], 0, image.shape[1])
        crop_bbox[:, 1] = np.clip(crop_bbox[:, 1], 0, image.shape[0])
        sub_img = image[crop_bbox[0][1]:crop_bbox[1][1], crop_bbox[0][0]:crop_bbox[1][0], :]
        if sub_img.size == 0:
            return None, crop_bbox, np.array([0, 0], dtype=np.int32)
        side_len = int(np.linalg.norm(sub_img.shape[:2])) if for_rotation else int(max(sub_img.shape[:2]))
        side_len = max(1, side_len)
        pad_h = side_len - sub_img.shape[0]
        pad_w = side_len - sub_img.shape[1]
        left = pad_w // 2
        top = pad_h // 2
        right = pad_w - left
        bottom = pad_h - top
        padded = cv2.copyMakeBorder(sub_img, top, bottom, left, right, cv2.BORDER_CONSTANT, None, (0, 0, 0))
        bias = crop_bbox[0] - np.array([left, top], dtype=np.int32)
        return padded, crop_bbox, bias

    def detect_hands_21_landmarks(self, frame_bgr: np.ndarray, conf_thresh: float = 0.55):
        """Retorna lista de arrays (21, 3) com os 21 landmarks [x, y, z] de cada mão detectada."""
        if not self.ready:
            return []
        palms = self._detect_palms(frame_bgr)
        hands_pts = []
        for palm_bbox, palm_lms, _ in palms:
            pad_bias = np.array([0, 0], dtype=np.int32)
            cropped, cur_bbox, bias = self._crop_and_pad_from_palm(frame_bgr, palm_bbox.copy(), True)
            if cropped is None:
                continue
            rgb_cropped = cv2.cvtColor(cropped, cv2.COLOR_BGR2RGB)
            pad_bias = pad_bias + bias
            cur_bbox = cur_bbox - pad_bias
            lms_local = palm_lms - pad_bias

            p1 = lms_local[0]  # Base da palma (WRIST)
            p2 = lms_local[2]  # Base do dedo médio (MIDDLE_MCP)
            radians = (np.pi * 0.5) - np.arctan2(-(p2[1] - p1[1]), p2[0] - p1[0])
            radians = radians - 2.0 * np.pi * np.floor((radians + np.pi) / (2.0 * np.pi))
            angle = float(np.rad2deg(radians))

            center_bbox = np.sum(cur_bbox, axis=0) * 0.5
            rot_mat = cv2.getRotationMatrix2D((float(center_bbox[0]), float(center_bbox[1])), angle, 1.0)
            rotated_img = cv2.warpAffine(rgb_cropped, rot_mat, (rgb_cropped.shape[1], rgb_cropped.shape[0]))

            homog = np.c_[lms_local, np.ones(lms_local.shape[0])]
            rot_lms = np.array([np.dot(homog, rot_mat[0]), np.dot(homog, rot_mat[1])])
            rot_bbox = np.array([np.amin(rot_lms, axis=1), np.amax(rot_lms, axis=1)])

            final_crop, rot_bbox, _ = self._crop_and_pad_from_palm(rotated_img, rot_bbox, False)
            if final_crop is None or final_crop.size == 0:
                continue
            blob = cv2.resize(final_crop, (224, 224), interpolation=cv2.INTER_AREA).astype(np.float32) / 255.0
            blob = blob[np.newaxis, :, :, :]

            raw_lms, raw_conf, _, _ = self.hand_sess.run(None, {"input_1": blob})
            conf = float(raw_conf[0][0])
            if conf < conf_thresh:
                continue

            lms_21 = raw_lms[0].reshape(21, 3).copy()
            wh_rot = rot_bbox[1] - rot_bbox[0]
            scale_factor = float(max(wh_rot / self.hand_input_size))
            lms_21[:, :2] = (lms_21[:, :2] - (self.hand_input_size * 0.5)) * scale_factor
            lms_21[:, 2] = lms_21[:, 2] * scale_factor

            coords_rot = cv2.getRotationMatrix2D((0.0, 0.0), angle, 1.0)
            rot_pts_xy = np.dot(lms_21[:, :2], coords_rot[:, :2])

            rot_comp = np.array([[rot_mat[0][0], rot_mat[1][0]], [rot_mat[0][1], rot_mat[1][1]]])
            trans_comp = np.array([rot_mat[0][2], rot_mat[1][2]])
            inv_trans = np.array([
                -np.dot(rot_comp[0], trans_comp),
                -np.dot(rot_comp[1], trans_comp)
            ])
            inv_rot_mat = np.c_[rot_comp, inv_trans]
            center_h = np.append(np.sum(rot_bbox, axis=0) * 0.5, 1.0)
            orig_center = np.array([np.dot(center_h, inv_rot_mat[0]), np.dot(center_h, inv_rot_mat[1])])

            lms_21[:, :2] = rot_pts_xy + orig_center + pad_bias
            hands_pts.append(lms_21)
        return hands_pts


def get_hand_engine() -> MediaPipeHandEngineONNX:
    global _hand_engine_singleton
    if _hand_engine_singleton is None:
        _hand_engine_singleton = MediaPipeHandEngineONNX()
    return _hand_engine_singleton


def detect_hand_and_shhh_gestures(frame_bgr, curr_gray, fx, fy, fw, fh, nose_x, nose_y, rm_x, rm_y, lm_x, lm_y):
    """
    Detecta os 4 gestos manuais usando o motor neural MediaPipe HandPose 21-Keypoints ONNX
    (com fallback geométrico à prova de barba/pescoço):
    - gesture_shhh (🤫): Dedo indicador vertical cruzando os lábios/nariz (outros dedos dobrados)
    - gesture_thumbs_up (👍): Polegar apontando para cima + 4 dedos fechados no punho
    - gesture_victory (✌️): Indicador e médio estendidos em V + anelar e mínimo dobrados
    - gesture_open_palm (✋): Palma aberta com 4 dedos estendidos
    """
    gesture_shhh = 0
    gesture_thumbs_up = 0
    gesture_victory = 0
    gesture_open_palm = 0

    mouth_cx = float((rm_x + lm_x) * 0.5)
    mouth_cy = float((rm_y + lm_y) * 0.5)

    # -------------------------------------------------------------------------
    # CAMADA 1: Motor Neural MediaPipe 21-Keypoints 3D (ONNXRuntime)
    # -------------------------------------------------------------------------
    try:
        engine = get_hand_engine()
        if engine.ready:
            hands_list = engine.detect_hands_21_landmarks(frame_bgr, conf_thresh=0.52)
            for pts in hands_list:
                # Índices MediaPipe:
                # 0=WRIST | 1..4=THUMB | 5..8=INDEX | 9..12=MIDDLE | 13..16=RING | 17..20=PINKY
                wrist = pts[0, :2]
                palm_center = np.mean(pts[[0, 5, 9, 13, 17], :2], axis=0)
                hand_size = max(12.0, float(np.linalg.norm(pts[9, :2] - wrist)))

                def _finger_extended_up(mcp_i, pip_i, dip_i, tip_i) -> bool:
                    d_tip_w = float(np.linalg.norm(pts[tip_i, :2] - wrist))
                    d_pip_w = float(np.linalg.norm(pts[pip_i, :2] - wrist))
                    d_tip_m = float(np.linalg.norm(pts[tip_i, :2] - pts[mcp_i, :2]))
                    d_pip_m = float(np.linalg.norm(pts[pip_i, :2] - pts[mcp_i, :2]))
                    # Dedo estendido e apontando para cima
                    return (
                        (d_tip_w > d_pip_w * 1.08)
                        and (d_tip_m > d_pip_m * 1.18)
                        and (pts[tip_i, 1] < pts[pip_i, 1] - hand_size * 0.04)
                    )

                def _finger_curled(mcp_i, pip_i, dip_i, tip_i) -> bool:
                    d_tip_w = float(np.linalg.norm(pts[tip_i, :2] - wrist))
                    d_pip_w = float(np.linalg.norm(pts[pip_i, :2] - wrist))
                    d_tip_p = float(np.linalg.norm(pts[tip_i, :2] - palm_center))
                    d_mcp_p = float(np.linalg.norm(pts[mcp_i, :2] - palm_center))
                    return (
                        (d_tip_w < d_pip_w * 1.16)
                        or (d_tip_p < d_mcp_p * 1.38)
                        or (pts[tip_i, 1] > pts[pip_i, 1] - hand_size * 0.02)
                    )

                index_up = _finger_extended_up(5, 6, 7, 8)
                middle_up = _finger_extended_up(9, 10, 11, 12)
                ring_up = _finger_extended_up(13, 14, 15, 16)
                pinky_up = _finger_extended_up(17, 18, 19, 20)

                index_curled = _finger_curled(5, 6, 7, 8)
                middle_curled = _finger_curled(9, 10, 11, 12)
                ring_curled = _finger_curled(13, 14, 15, 16)
                pinky_curled = _finger_curled(17, 18, 19, 20)

                curled_count = int(index_curled) + int(middle_curled) + int(ring_curled) + int(pinky_curled)
                up_count = int(index_up) + int(middle_up) + int(ring_up) + int(pinky_up)

                # Verifica se o polegar (1..4) está apontando para cima e destacado do punho
                thumb_tip = pts[4, :2]
                thumb_ip = pts[3, :2]
                thumb_mcp = pts[2, :2]
                thumb_pointing_up = (
                    (thumb_tip[1] < thumb_ip[1] - hand_size * 0.03)
                    and (thumb_tip[1] < thumb_mcp[1] - hand_size * 0.14)
                    and (thumb_tip[1] < min(pts[5, 1], pts[9, 1]) + hand_size * 0.12)
                    and (float(np.linalg.norm(thumb_tip - pts[9, :2])) > hand_size * 0.38)
                )
                # Verifica também se nenhum dos 4 dedos está muito acima da ponta do polegar
                fingers_below_thumb = all(
                    pts[t_idx, 1] > (thumb_tip[1] - hand_size * 0.05) for t_idx in (8, 12, 16, 20)
                )

                # Verifica se o dedo indicador (ponta 8, DIP 7 ou PIP 6) está sobre a boca/nariz
                idx_near_mouth = False
                for pt_idx in (pts[8, :2], pts[7, :2], 0.5 * (pts[8, :2] + pts[7, :2])):
                    if (
                        abs(float(pt_idx[0]) - mouth_cx) <= (fw * 0.42)
                        and (nose_y - fh * 0.20) <= float(pt_idx[1]) <= (mouth_cy + fh * 0.35)
                    ):
                        idx_near_mouth = True
                        break

                # (A) 👍 JOINHA (Thumbs Up): Polegar apontando para cima + dedos fechados no punho
                if thumb_pointing_up and fingers_below_thumb and (up_count == 0 or (curled_count >= 3 and not middle_up and not ring_up)):
                    gesture_thumbs_up = 1
                # (B) ✌️ VITÓRIA / PAZ (V-Sign): Indicador + Médio para cima, Anelar + Mínimo dobrados
                elif index_up and middle_up and not ring_up and not pinky_up:
                    v_spread = float(np.linalg.norm(pts[8, :2] - pts[12, :2]))
                    if v_spread >= hand_size * 0.15:
                        gesture_victory = 1
                # (C) ✋ PALMA ABERTA (Stop / Pause): Todos os 4 dedos estendidos para cima
                elif index_up and middle_up and ring_up and pinky_up:
                    gesture_open_palm = 1
                # (D) 🤫 SILÊNCIO (Shhh - Dedo na Boca): Indicador estendido sobre os lábios e demais dedos dobrados
                elif index_up and not middle_up and not ring_up and not pinky_up and idx_near_mouth and (wrist[1] > mouth_cy):
                    gesture_shhh = 1

            # Se o motor ONNX detectou qualquer gesto de mão, retorna imediatamente com prioridade limpa
            if gesture_thumbs_up or gesture_victory or gesture_open_palm or gesture_shhh:
                if gesture_thumbs_up or gesture_victory or gesture_open_palm:
                    gesture_shhh = 0
                return gesture_shhh, gesture_thumbs_up, gesture_victory, gesture_open_palm
    except Exception:
        pass

    # -------------------------------------------------------------------------
    # CAMADA 2: Fallback Geométrico YCrCb + Sobel (À Prova de Barba e Pescoço)
    # -------------------------------------------------------------------------
    try:
        ycrcb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2YCrCb)
        fc_x0 = max(0, int(fx + fw * 0.25))
        fc_x1 = min(FRAME_WIDTH, int(fx + fw * 0.75))
        fc_y0 = max(0, int(fy + fh * 0.20))
        fc_y1 = min(FRAME_HEIGHT, int(fy + fh * 0.60))
        if (fc_x1 - fc_x0) >= 8 and (fc_y1 - fc_y0) >= 8:
            face_ycrcb = ycrcb[fc_y0:fc_y1, fc_x0:fc_x1]
            med_cr = float(np.median(face_ycrcb[:, :, 1]))
            med_cb = float(np.median(face_ycrcb[:, :, 2]))
            cr_min = max(125, int(med_cr - 24))
            cr_max = min(185, int(med_cr + 24))
            cb_min = max(70, int(med_cb - 24))
            cb_max = min(135, int(med_cb + 24))
        else:
            cr_min, cr_max = 132, 178
            cb_min, cb_max = 76, 128

        skin_mask = cv2.inRange(
            ycrcb,
            np.array([35, cr_min, cb_min], dtype=np.uint8),
            np.array([250, cr_max, cb_max], dtype=np.uint8),
        )
        kernel = np.ones((3, 3), dtype=np.uint8)
        skin_mask = cv2.morphologyEx(skin_mask, cv2.MORPH_OPEN, kernel)
        skin_mask = cv2.morphologyEx(skin_mask, cv2.MORPH_CLOSE, kernel)

        # Mascara apenas a elipse facial e a base lisa do pescoço, preservando mãos ao lado ou à frente do peito
        hand_skin = skin_mask.copy()
        face_cx_i = int(round(fx + fw * 0.5))
        face_cy_i = int(round(fy + fh * 0.48))
        cv2.ellipse(
            hand_skin,
            (face_cx_i, face_cy_i),
            (int(round(fw * 0.56)), int(round(fh * 0.55))),
            0, 0, 360, 0, -1,
        )
        neck_x0 = max(0, int(round(fx + fw * 0.16)))
        neck_x1 = min(FRAME_WIDTH, int(round(fx + fw * 0.84)))
        neck_y0 = max(0, int(round(fy + fh * 0.90)))
        hand_skin[neck_y0:FRAME_HEIGHT, neck_x0:neck_x1] = 0

        contours, _ = cv2.findContours(hand_skin, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        if contours:
            best_cnt = max(contours, key=cv2.contourArea)
            area = float(cv2.contourArea(best_cnt))
            min_hand_area = max(320.0, (fw * fh) * 0.13)
            if area >= min_hand_area:
                hx, hy, hw, hh = cv2.boundingRect(best_cnt)
                sub_mask = np.zeros((hh, hw), dtype=np.uint8)
                cv2.drawContours(sub_mask, [best_cnt - [hx, hy]], -1, 255, -1)
                # Corta eventual antebraço comprido na parte inferior do contorno
                eff_hh = min(hh, max(16, int(round(hw * 1.75))))
                sub_mask = sub_mask[0:eff_hh, :]
                aspect = float(eff_hh) / float(max(1, hw))

                hull = cv2.convexHull(best_cnt)
                hull_area = max(1.0, float(cv2.contourArea(hull)))
                solidity = area / hull_area

                split_y = max(4, int(round(eff_hh * 0.36)))
                top_slice = sub_mask[0:split_y, :]
                bot_slice = sub_mask[split_y:eff_hh, :]

                top_fill = float((top_slice > 0).sum()) / float(max(1, top_slice.size))
                bot_fill = float((bot_slice > 0).sum()) / float(max(1, bot_slice.size))

                mid_top_row = top_slice[max(0, split_y // 2), :]
                runs = 0
                in_run = False
                run_len = 0
                for px_val in mid_top_row:
                    if px_val > 0:
                        run_len += 1
                        if not in_run and run_len >= 2:
                            runs += 1
                            in_run = True
                    else:
                        in_run = False
                        run_len = 0

                deep_defects = 0
                hull_idx = cv2.convexHull(best_cnt, returnPoints=False)
                if hull_idx is not None and len(hull_idx) > 3 and len(best_cnt) >= 5:
                    defects = cv2.convexityDefects(best_cnt, hull_idx)
                    if defects is not None:
                        for i in range(defects.shape[0]):
                            _, _, f_idx, depth_fix = defects[i, 0]
                            depth_px = float(depth_fix) / 256.0
                            far_pt = best_cnt[f_idx][0]
                            if depth_px >= max(7.0, eff_hh * 0.15) and (far_pt[1] - hy) <= (eff_hh * 0.72):
                                deep_defects += 1

                if runs == 1 and deep_defects <= 1 and 0.10 <= top_fill <= 0.52 and bot_fill >= 0.42 and 0.78 <= aspect <= 2.60:
                    gesture_thumbs_up = 1
                elif (runs == 2 or deep_defects == 1) and 0.18 <= top_fill <= 0.68 and aspect >= 0.95 and solidity < 0.88:
                    gesture_victory = 1
                elif (runs >= 3 or deep_defects >= 2) and area >= (min_hand_area * 1.35) and 0.45 <= solidity <= 0.86:
                    gesture_open_palm = 1

        # Fallback de Dedo na Boca ("Shhh"): exige coluna central estreita de pele CLARA contrastando com os lados da boca/barba
        if not (gesture_thumbs_up or gesture_victory or gesture_open_palm):
            mcx_i = int(round(mouth_cx))
            mcy_i = int(round(mouth_cy))
            my0 = max(0, int(round(nose_y + (mcy_i - nose_y) * 0.25)))
            my1 = min(FRAME_HEIGHT, int(round(fy + fh * 0.96)))
            c_x0 = max(0, mcx_i - max(3, int(round(fw * 0.06))))
            c_x1 = min(FRAME_WIDTH, mcx_i + max(4, int(round(fw * 0.06))))
            fl_x0 = max(0, mcx_i - max(10, int(round(fw * 0.24))))
            fl_x1 = max(0, mcx_i - max(5, int(round(fw * 0.11))))
            fr_x0 = min(FRAME_WIDTH, mcx_i + max(5, int(round(fw * 0.11))))
            fr_x1 = min(FRAME_WIDTH, mcx_i + max(10, int(round(fw * 0.24))))

            if (my1 - my0) >= 8 and (c_x1 - c_x0) >= 4 and (fl_x1 - fl_x0) >= 3 and (fr_x1 - fr_x0) >= 3:
                center_skin = skin_mask[my0:my1, c_x0:c_x1]
                flank_l_skin = skin_mask[my0:my1, fl_x0:fl_x1]
                flank_r_skin = skin_mask[my0:my1, fr_x0:fr_x1]
                c_skin_ratio = float((center_skin > 0).sum()) / float(max(1, center_skin.size))
                f_skin_ratio = 0.5 * (
                    float((flank_l_skin > 0).sum()) / float(max(1, flank_l_skin.size))
                    + float((flank_r_skin > 0).sum()) / float(max(1, flank_r_skin.size))
                )
                c_luma = float(np.mean(curr_gray[my0:my1, c_x0:c_x1]))
                f_luma = 0.5 * (
                    float(np.mean(curr_gray[my0:my1, fl_x0:fl_x1]))
                    + float(np.mean(curr_gray[my0:my1, fr_x0:fr_x1]))
                )
                mouth_roi = curr_gray[my0:my1, fl_x0:fr_x1]
                sobel_x = float(np.mean(np.abs(cv2.Sobel(mouth_roi, cv2.CV_32F, 1, 0, ksize=3))))
                sobel_y = float(np.mean(np.abs(cv2.Sobel(mouth_roi, cv2.CV_32F, 0, 1, ksize=3)))) + 1.0

                if (
                    c_skin_ratio >= 0.78
                    and (c_skin_ratio - f_skin_ratio) >= 0.42
                    and (c_luma - f_luma) >= 16.0
                    and (sobel_x / sobel_y) >= 1.38
                ):
                    gesture_shhh = 1
    except Exception:
        pass

    return gesture_shhh, gesture_thumbs_up, gesture_victory, gesture_open_palm


def analyze_vision_frame(detector_tuple, frame_bgr, prev_gray=None, prev_cx=160):
    """
    Extrai métricas visuais completas em tempo real (320x240):
    - Multi-face tracking (usuário principal + 2º rosto / intruso)
    - Roll (inclinação lateral) e Yaw (direção horizontal do olhar)
    - Sorriso e Bocejo / Fadiga visual (abertura vertical da cavidade oral)
    - Aceno de mão periférico e Objeto aproximado da lente para inspeção
    - Gestos manuais: Shhh (dedo na boca), Joinha (👍), Vitória (✌️) e Palma Aberta (✋)
    - Luminosidade ambiente estimada em Lux (0..1000) e % de mudança de cena
    """
    curr_gray = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2GRAY)

    # 1. Luminosidade ambiente real em Lux aproximado (média + percentil 90 do sensor)
    mean_lum = float(np.mean(curr_gray))
    p90_lum = float(np.percentile(curr_gray, 90))
    ambient_lux = int(round(max(2.0, min(999.0, (mean_lum * 2.2) + (p90_lum * 0.85) - 25.0))))

    # 2. Mudança visual de cena (diff percentual global) e detecção de objeto próximo no centro
    scene_change_pct = 0
    object_close = 0
    if prev_gray is not None:
        global_diff = cv2.absdiff(curr_gray, prev_gray)
        changed_px = int((global_diff > 24).sum())
        scene_change_pct = min(100, int(round((changed_px * 100.0) / float(curr_gray.size))))

    # Verifica nitidez/textura de bordas no quadrante central inferior (onde objetos/canecas são mostrados)
    center_roi = curr_gray[int(FRAME_HEIGHT * 0.28):int(FRAME_HEIGHT * 0.88),
                           int(FRAME_WIDTH * 0.25):int(FRAME_WIDTH * 0.75)]
    laplacian_var = float(cv2.Laplacian(center_roi, cv2.CV_64F).var())

    det_type, detector = detector_tuple
    if det_type == "yunet" and detector is not None:
        _, faces = detector.detect(frame_bgr)
        if faces is not None and len(faces) > 0:
            # Ordena rostos por área decrescente: [0] = usuário principal, [1] = 2ª pessoa/intruso
            sorted_faces = sorted(faces, key=lambda r: float(r[2]) * float(r[3]), reverse=True)
            face_count = min(9, len(sorted_faces))

            best_face = sorted_faces[0]
            fx, fy, fw, fh = float(best_face[0]), float(best_face[1]), float(best_face[2]), float(best_face[3])
            re_x, re_y = float(best_face[4]), float(best_face[5])
            le_x, le_y = float(best_face[6]), float(best_face[7])
            nose_x, nose_y = float(best_face[8]), float(best_face[9])
            rm_x, rm_y = float(best_face[10]), float(best_face[11])
            lm_x, lm_y = float(best_face[12]), float(best_face[13])

            cx = int(round(fx + (fw * 0.5)))
            cy = int(round(fy + (fh * 0.45)))
            face_w_int = max(16, min(255, int(round(fw))))

            # Coordenadas do 2º rosto (se houver outra pessoa atrás/ao lado)
            sec_x, sec_y = 0, 0
            if face_count >= 2:
                sf = sorted_faces[1]
                sfx = int(round(float(sf[0]) + float(sf[2]) * 0.5))
                sfy = int(round(float(sf[1]) + float(sf[3]) * 0.45))
                sec_x = map_cam_x_to_screen(sfx)
                sec_y = map_cam_y_to_screen(sfy)

            # 1. Roll: inclinação lateral da cabeça
            dy_eyes = le_y - re_y
            dx_eyes = max(1.0, le_x - re_x)
            tilt_deg = math.degrees(math.atan2(dy_eyes, dx_eyes))
            tilt_deg = max(-35.0, min(35.0, tilt_deg))

            # 2. Yaw: direção horizontal do olhar (-45 esq .. +45 dir no referencial espelhado)
            eyes_mid_x = (re_x + le_x) * 0.5
            eye_span = max(8.0, abs(le_x - re_x))
            # Assimetria do nariz em relação ao ponto médio dos olhos
            raw_yaw = ((nose_x - eyes_mid_x) / (eye_span * 0.55)) * 42.0
            yaw_deg = max(-45.0, min(45.0, -raw_yaw))

            # 3. Métricas brutas de postura (serão refinadas pela calibração de baseline no C++ e no tracker)
            too_close = 1 if fw >= 126.0 else 0
            slouching = 1 if (cy >= 166 or (fw >= 110.0 and cy >= 150)) else 0

            # 4. Detecção de Sorriso (razão largura da boca / distância interocular + elevação dos cantos)
            eye_dist = math.hypot(le_x - re_x, le_y - re_y)
            mouth_w = math.hypot(lm_x - rm_x, lm_y - rm_y)
            mouth_eye_ratio = mouth_w / max(1.0, eye_dist)
            is_smiling = 1 if mouth_eye_ratio >= 0.90 else 0

            # 5. Detecção de Bocejo / Cansaço (cavidade escura aberta verticalmente no centro da boca)
            is_yawning = 0
            mx0 = max(0, int(min(rm_x, lm_x) + 2))
            mx1 = min(FRAME_WIDTH, int(max(rm_x, lm_x) - 2))
            my0 = max(0, int(min(rm_y, lm_y) - int(fh * 0.05)))
            my1 = min(FRAME_HEIGHT, int(max(rm_y, lm_y) + int(fh * 0.14)))
            if (mx1 - mx0) >= 8 and (my1 - my0) >= 6:
                mouth_patch = curr_gray[my0:my1, mx0:mx1]
                face_median = float(np.median(curr_gray[max(0, int(fy)):min(FRAME_HEIGHT, int(fy + fh)),
                                                        max(0, int(fx)):min(FRAME_WIDTH, int(fx + fw))]))
                dark_thresh = max(18.0, face_median * 0.46)
                dark_ratio = float((mouth_patch < dark_thresh).sum()) / float(mouth_patch.size)
                # Bocejo: boca estreita/média horizontalmente mas com cavidade oral escura bem aberta
                if dark_ratio >= 0.36 and mouth_eye_ratio < 0.88 and abs(yaw_deg) < 22.0:
                    is_yawning = 1

            # 6. Detecção de Cabeça Baixa / Olhando Celular ("Doomscrolling")
            eyes_mid_y = (re_y + le_y) * 0.5
            mouth_mid_y = (rm_y + lm_y) * 0.5
            nose_to_mouth_norm = (mouth_mid_y - nose_y) / max(1.0, fh)
            eye_to_nose_norm = (nose_y - eyes_mid_y) / max(1.0, fh)
            is_looking_down = 1 if (nose_to_mouth_norm < 0.135 and eye_to_nose_norm > 0.39 and cy >= 154) else 0

            # 7. Detecção de Gestos Manuais (Shhh na boca, Joinha, Vitória, Palma Aberta)
            g_shhh, g_thumb, g_victory, g_palm = detect_hand_and_shhh_gestures(
                frame_bgr, curr_gray, fx, fy, fw, fh, nose_x, nose_y, rm_x, rm_y, lm_x, lm_y
            )
            if g_shhh:
                is_yawning = 0  # Dedo na boca não é bocejo

            # 8. Detecção de Aceno de Mão (Hand Wave) na periferia superior e Objeto Mostrado na Frente
            hand_wave = 0
            if prev_gray is not None and abs(cx - prev_cx) <= 12:
                diff = cv2.absdiff(curr_gray, prev_gray)
                x0 = max(0, int(fx - 18))
                y0 = max(0, int(fy - 14))
                x1 = min(FRAME_WIDTH, int(fx + fw + 18))
                y1 = min(FRAME_HEIGHT, int(fy + fh + 20))
                diff_periph = diff.copy()
                diff_periph[y0:y1, x0:x1] = 0
                upper_roi = diff_periph[0:int(FRAME_HEIGHT * 0.75), :]
                active_pixels = int((upper_roi > 28).sum())
                if upper_roi.size > 0:
                    motion_ratio = active_pixels / float(upper_roi.size)
                    if 0.048 <= motion_ratio <= 0.30 and not (g_thumb or g_victory or g_shhh):
                        hand_wave = 1

                # Objeto aproximado na frente do peito/câmera (abaixo ou ao lado do rosto com alta textura)
                lower_fg_roi = diff[int(FRAME_HEIGHT * 0.45):FRAME_HEIGHT,
                                    int(FRAME_WIDTH * 0.22):int(FRAME_WIDTH * 0.78)]
                fg_motion = float((lower_fg_roi > 25).sum()) / float(max(1, lower_fg_roi.size))
                if fg_motion >= 0.12 and laplacian_var >= 160.0 and not hand_wave and not g_shhh:
                    object_close = 1

            return (
                True, cx, cy, tilt_deg, slouching, too_close, is_smiling,
                is_looking_down, hand_wave, face_count, sec_x, sec_y,
                yaw_deg, face_w_int, is_yawning, object_close,
                ambient_lux, scene_change_pct, g_shhh, g_thumb, g_victory, g_palm, curr_gray
            )

        # Sem rosto detectado: se houver objeto muito nítido/próximo ocupando o centro da lente, sinaliza object_close
        if laplacian_var >= 220.0 and scene_change_pct >= 6:
            object_close = 1
        return (
            False, 0, 0, 0.0, 0, 0, 0, 0, 0, 0, 0, 0,
            0.0, 0, 0, object_close, ambient_lux, scene_change_pct, 0, 0, 0, 0, curr_gray
        )

    if det_type == "haar" and detector is not None:
        gray = cv2.equalizeHist(curr_gray)
        faces = detector.detectMultiScale(gray, scaleFactor=1.15, minNeighbors=4, minSize=(36, 36))
        if len(faces) > 0:
            sorted_faces = sorted(faces, key=lambda rect: rect[2] * rect[3], reverse=True)
            face_count = min(9, len(sorted_faces))
            fx, fy, fw, fh = sorted_faces[0]
            cx = int(fx + (fw // 2))
            cy = int(fy + (fh // 2))
            sec_x, sec_y = 0, 0
            if face_count >= 2:
                sfx = int(sorted_faces[1][0] + (sorted_faces[1][2] // 2))
                sfy = int(sorted_faces[1][1] + (sorted_faces[1][3] // 2))
                sec_x = map_cam_x_to_screen(sfx)
                sec_y = map_cam_y_to_screen(sfy)
            too_close = 1 if fw >= 126 else 0
            slouching = 1 if cy >= 166 else 0
            g_shhh, g_thumb, g_victory, g_palm = detect_hand_and_shhh_gestures(
                frame_bgr, curr_gray, float(fx), float(fy), float(fw), float(fh),
                float(cx), float(fy + fh * 0.58),
                float(fx + fw * 0.35), float(fy + fh * 0.76),
                float(fx + fw * 0.65), float(fy + fh * 0.76)
            )
            return (
                True, cx, cy, 0.0, slouching, too_close, 0, 0, 0,
                face_count, sec_x, sec_y, 0.0, int(fw), 0, object_close,
                ambient_lux, scene_change_pct, g_shhh, g_thumb, g_victory, g_palm, curr_gray
            )

    return (
        False, 0, 0, 0.0, 0, 0, 0, 0, 0, 0, 0, 0,
        0.0, 0, 0, object_close, ambient_lux, scene_change_pct, 0, 0, 0, 0, curr_gray
    )


def run_continuous_face_tracker() -> int:
    """Loop contínuo em background que rastreia rosto, postura, gestos, multi-faces, lux e PIP RGB565."""
    detector_tuple = create_face_detector()
    if detector_tuple[0] == "none":
        return 1

    cap = cv2.VideoCapture(0, cv2.CAP_DSHOW if sys.platform.startswith("win") else cv2.CAP_ANY)
    if not cap.isOpened():
        cap = cv2.VideoCapture(0)
    if not cap.isOpened():
        return 1

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT)

    try:
        print(
            f"[WEBCAM-TRACKER] Visao 2.0 + Gestos Manuais Iniciada ({detector_tuple[0].upper()} 320x240) "
            f"-> {FACE_TRACK_FILE}"
        )
    except Exception:
        pass

    smoothed_x = FRAME_WIDTH // 2
    smoothed_y = FRAME_HEIGHT // 2
    smoothed_tilt = 0.0
    smoothed_yaw = 0.0
    smoothed_lux = 220
    last_slouch = 0
    last_close = 0
    last_smile = 0
    last_looking_down = 0
    last_face_count = 0
    last_sec_x = 0
    last_sec_y = 0
    last_face_w = 68
    yawn_frames_acc = 0
    obj_frames_acc = 0
    wave_frames_acc = 0
    shhh_frames_acc = 0
    thumb_frames_acc = 0
    victory_frames_acc = 0
    palm_frames_acc = 0
    prev_gray_frame = None
    prev_raw_cx = FRAME_WIDTH // 2
    last_snap_saved_time = 0.0
    last_pip_saved_time = 0.0
    last_telemetry_time = 0.0
    last_face_seen_time = 0.0

    cpu_pct, ram_pct, active_win, git_dirty = query_host_pc_telemetry()

    try:
        while True:
            ok, frame = cap.read()
            now_sec = time.time()
            if not ok or frame is None:
                time.sleep(0.08)
                continue

            frame_small = cv2.resize(frame, (FRAME_WIDTH, FRAME_HEIGHT))
            (
                detected,
                center_x,
                center_y,
                tilt_deg,
                is_slouching,
                is_too_close,
                is_smiling,
                is_looking_down,
                hand_wave_raw,
                face_count,
                sec_x,
                sec_y,
                yaw_deg,
                face_w_int,
                is_yawning_raw,
                object_close_raw,
                ambient_lux,
                scene_change_pct,
                g_shhh_raw,
                g_thumb_raw,
                g_victory_raw,
                g_palm_raw,
                prev_gray_frame,
            ) = analyze_vision_frame(detector_tuple, frame_small, prev_gray_frame, prev_raw_cx)

            smoothed_lux = int(round(smoothed_lux * 0.75 + ambient_lux * 0.25))

            face_detected = 0
            hand_wave_confirmed = 0
            yawn_confirmed = 0
            object_close_confirmed = 0
            shhh_confirmed = 0
            thumb_confirmed = 0
            victory_confirmed = 0
            palm_confirmed = 0

            if object_close_raw:
                obj_frames_acc = min(12, obj_frames_acc + 2)
            else:
                obj_frames_acc = max(0, obj_frames_acc - 1)
            if obj_frames_acc >= 5:
                object_close_confirmed = 1

            if detected:
                prev_raw_cx = center_x
                clamped_x = map_cam_x_to_screen(center_x)
                clamped_y = map_cam_y_to_screen(center_y)

                smoothed_x = int(round(smoothed_x * 0.35 + clamped_x * 0.65))
                smoothed_y = int(round(smoothed_y * 0.35 + clamped_y * 0.65))
                smoothed_tilt = (smoothed_tilt * 0.55) + (tilt_deg * 0.45)
                smoothed_yaw = (smoothed_yaw * 0.55) + (yaw_deg * 0.45)
                last_slouch = is_slouching
                last_close = is_too_close
                last_smile = is_smiling
                last_looking_down = is_looking_down
                last_face_count = face_count
                last_sec_x = sec_x
                last_sec_y = sec_y
                last_face_w = int(round(last_face_w * 0.5 + face_w_int * 0.5))

                if hand_wave_raw:
                    wave_frames_acc = min(10, wave_frames_acc + 2)
                else:
                    wave_frames_acc = max(0, wave_frames_acc - 1)
                if wave_frames_acc >= 4:
                    hand_wave_confirmed = 1

                if is_yawning_raw:
                    yawn_frames_acc = min(14, yawn_frames_acc + 2)
                else:
                    yawn_frames_acc = max(0, yawn_frames_acc - 1)
                if yawn_frames_acc >= 6:
                    yawn_confirmed = 1

                # Acumuladores de gestos manuais com prioridade para Joinha/Vitória/Palma (~140ms) e proteção anti-falso-positivo no Shhh
                if g_thumb_raw or g_victory_raw or g_palm_raw:
                    shhh_frames_acc = 0
                else:
                    shhh_frames_acc = min(10, shhh_frames_acc + 2) if g_shhh_raw else max(0, shhh_frames_acc - 1)
                thumb_frames_acc = min(10, thumb_frames_acc + 2) if g_thumb_raw else max(0, thumb_frames_acc - 1)
                victory_frames_acc = min(10, victory_frames_acc + 2) if g_victory_raw else max(0, victory_frames_acc - 1)
                palm_frames_acc = min(10, palm_frames_acc + 2) if g_palm_raw else max(0, palm_frames_acc - 1)

                if thumb_frames_acc >= 3:
                    thumb_confirmed = 1
                elif victory_frames_acc >= 3:
                    victory_confirmed = 1
                elif palm_frames_acc >= 3:
                    palm_confirmed = 1
                elif shhh_frames_acc >= 5:
                    shhh_confirmed = 1

                face_detected = 1
                last_face_seen_time = now_sec
            elif (now_sec - last_face_seen_time) < 1.5:
                face_detected = 1
            else:
                smoothed_tilt *= 0.8
                smoothed_yaw *= 0.8
                last_slouch = 0
                last_close = 0
                last_smile = 0
                last_looking_down = 0
                last_face_count = 0
                last_sec_x = 0
                last_sec_y = 0
                wave_frames_acc = 0
                yawn_frames_acc = 0
                shhh_frames_acc = 0
                thumb_frames_acc = 0
                victory_frames_acc = 0
                palm_frames_acc = 0

            if (now_sec - last_telemetry_time) >= TELEMETRY_UPDATE_INTERVAL_SEC:
                last_telemetry_time = now_sec
                cpu_pct, ram_pct, active_win, git_dirty = query_host_pc_telemetry()

            # Atualiza face_track.txt com os 26 campos numéricos na Linha 1 + título da janela na Linha 2
            line_data = (
                f"{int(now_sec)} {face_detected} {smoothed_x} {smoothed_y} "
                f"{smoothed_tilt:.1f} {last_slouch} {last_close} {cpu_pct} {ram_pct} "
                f"{last_smile} {last_looking_down} {hand_wave_confirmed} {git_dirty} "
                f"{last_face_count} {last_sec_x} {last_sec_y} {smoothed_yaw:.1f} "
                f"{last_face_w} {yawn_confirmed} {object_close_confirmed} "
                f"{smoothed_lux} {scene_change_pct} "
                f"{shhh_confirmed} {thumb_confirmed} {victory_confirmed} {palm_confirmed}\n"
                f"{active_win}\n"
            )
            try:
                with open(FACE_TRACK_FILE, "w", encoding="utf-8") as f:
                    f.write(line_data)
                    f.flush()
            except OSError:
                pass

            # Salva buffers PIP 64x48 e 160x120 tanto em Grayscale CLAHE quanto em Cor Real RGB565 (16-bit)
            if (now_sec - last_pip_saved_time) >= PIP_UPDATE_INTERVAL_SEC:
                last_pip_saved_time = now_sec
                try:
                    pip_flipped = cv2.flip(frame_small, 1)

                    # 1. Grayscale CLAHE (Modos Ciano Óptico e Termográfico)
                    pip_gray = cv2.cvtColor(pip_flipped, cv2.COLOR_BGR2GRAY)
                    clahe = cv2.createCLAHE(clipLimit=2.2, tileGridSize=(4, 4))
                    pip_enhanced = clahe.apply(pip_gray)
                    pip_small_gray = cv2.resize(pip_enhanced, (PIP_WIDTH, PIP_HEIGHT), interpolation=cv2.INTER_AREA)
                    pip_hd_gray = cv2.resize(pip_enhanced, (PIP_HD_WIDTH, PIP_HD_HEIGHT), interpolation=cv2.INTER_AREA)

                    with open(CAM_PIP_FILE, "wb") as pf:
                        pf.write(pip_small_gray.tobytes())
                        pf.flush()
                    with open(CAM_PIP_HD_FILE, "wb") as phf:
                        phf.write(pip_hd_gray.tobytes())
                        phf.flush()

                    # 2. Cor Real 16-bit RGB565 (Modo RGB565 Color HD)
                    pip_small_bgr = cv2.resize(pip_flipped, (PIP_WIDTH, PIP_HEIGHT), interpolation=cv2.INTER_AREA)
                    pip_hd_bgr = cv2.resize(pip_flipped, (PIP_HD_WIDTH, PIP_HD_HEIGHT), interpolation=cv2.INTER_AREA)
                    with open(CAM_PIP_RGB565_FILE, "wb") as crf:
                        crf.write(bgr_to_rgb565_bytes(pip_small_bgr))
                        crf.flush()
                    with open(CAM_PIP_HD_RGB565_FILE, "wb") as chrf:
                        chrf.write(bgr_to_rgb565_bytes(pip_hd_bgr))
                        chrf.flush()
                except OSError:
                    pass

            # Salva periodicamente o snapshot JPEG 320x240 (qualidade 84 para melhor leitura de objetos pelo Gemini)
            if (now_sec - last_snap_saved_time) >= SNAP_UPDATE_INTERVAL_SEC:
                last_snap_saved_time = now_sec
                try:
                    cv2.imwrite(CAM_SNAP_TMP_FILE, frame_small, [int(cv2.IMWRITE_JPEG_QUALITY), 84])
                    os.replace(CAM_SNAP_TMP_FILE, CAM_SNAP_FILE)
                except OSError:
                    try:
                        cv2.imwrite(CAM_SNAP_FILE, frame_small, [int(cv2.IMWRITE_JPEG_QUALITY), 84])
                    except OSError:
                        pass

            time.sleep(0.04)  # ~25 Hz de atualização de percepção visual
    except KeyboardInterrupt:
        pass
    finally:
        cap.release()
    return 0


if __name__ == "__main__":
    if "--snap-once" in sys.argv:
        success = capture_single_snapshot(CAM_SNAP_FILE)
        sys.exit(0 if success else 1)
    sys.exit(run_continuous_face_tracker())
